#include <linux/shadowpt.h>

#include <linux/io.h> /* for virt_to_phys() */
#include <linux/capability.h> /* for capable() */
#include <linux/syscalls.h> /* For SYSCALL_DEFINE */

/* Global kernel shadow page table structure  */
struct kern_shadow_pt kspt = {
	.active = false,
	.spt_lock = __SPIN_LOCK_UNLOCKED(kspt.spt_lock), /* protects fields */
	.status_lock = __MUTEX_INITIALIZER(kspt.status_lock), /* protects activation state */
	.inspector = NULL,
	.target = NULL,
	.target_mm = NULL,
	.target_mm_active = false,
	.user_shadow_pt = NULL,
	.kentries = NULL,
	.kentries_bytesize = 0
};

/* Check if spt active and mm is target, and if so grab spt lock.
 * Returns 0 on success, -1 if we don't match.
 */
static inline int enter_spt_lock(struct mm_struct *mm, unsigned long *flags)
{

	/* First check without lock to prevent every page fault contending for lock */
	if (!is_kspt_active || (!is_kspt_mm_active) || likely(!is_kspt_target_mm(mm)))
		return -1;


	spin_lock_irqsave(&kspt.spt_lock, *flags);

	if (!is_kspt_active || (!is_kspt_mm_active) || !is_kspt_target_mm(mm)) {
		spin_unlock_irqrestore(&kspt.spt_lock, *flags);
		return -1;
	}

	return 0;
}

/* Release spt lock, should only be called if enter_spt_lock returned 0. */
static inline void exit_spt_lock(unsigned long flags)
{
	spin_unlock_irqrestore(&kspt.spt_lock, flags);
}

/* VMA UPDATE FUNCTIONS */


/* Update an spte based on a vm_area struct, or NULL of spte should be marked deallocated. */
static void update_single_spte_vma(struct shadow_pte *spte, struct vm_area_struct *vma)
{
	WARN_ON(!spte);

	if (vma) {
		spte_set_allocated(spte, true);
		spte_set_anonymous(spte, vma_is_anonymous(vma));
		spte_set_vaddr_writeable(spte, vma->vm_flags & VM_WRITE);

	} else
		spte_set_allocated(spte, false);

}


/* Core update vma function, without input checking. Calls update_single_spt_vma for every
 * vaddr/spte in target range in provided VMA. Does not catch shrinking vmas.
 */
static void __update_kspt_vma(struct mm_struct *mm, struct vm_area_struct *vma)
{
	struct shadow_pte *spte;
	unsigned long update_start, update_end, curr_vaddr;

	mmap_assert_locked(mm);

	update_start = max_t(unsigned long, kspt_start_vaddr(&kspt), vma->vm_start);
	update_end = min_t(unsigned long, kspt_end_vaddr(&kspt), vma->vm_end);

	for (curr_vaddr = update_start; curr_vaddr < update_end; curr_vaddr += PAGE_SIZE) {
		spte = vaddr_to_kspte(&kspt, curr_vaddr);

		WARN_ON(!spte);

		update_single_spte_vma(spte, vma);
	}
}



/* Updates every entry in a single vma if kspt is active and mm struct is target_mm. Does not
 * update if vma shrank. Called in mmap.c, requires mm read lock.
 */
void update_kspt_vma(struct mm_struct *mm, struct vm_area_struct *vma)
{
	unsigned long flags;

	if (enter_spt_lock(mm, &flags) < 0)
		return;

	__update_kspt_vma(mm, vma);
	exit_spt_lock(flags);
}

/* Set a range of virtual addresses as deallocated in kspt. Start and end must be contained in
 * target range, and page aligned. End is the first byte out of range.
 */
static void kspt_mark_range_deallocated(unsigned long vstart, unsigned long vend)
{
	unsigned long vcurr;

	for (vcurr = vstart; vcurr < vend; vcurr += PAGE_SIZE) {
		struct shadow_pte *spte = vaddr_to_kspte(&kspt, vcurr);

		WARN_ON(!spte);

		if (spte)
			update_single_spte_vma(spte, NULL);
	}
}


/* Updates a full range of vmas, called from mmap.c and mprotect.c code. Detects shrinking VMAs.
 * Must be called while holding mm read lock.
 */
void update_kspt_vma_range(struct mm_struct *mm, unsigned long vstart, unsigned long vend)
{
	unsigned long flags, vlast;
	struct vm_area_struct *vma;

	VMA_ITERATOR(vmi, mm, vstart);

	/* Get lock if spt active and mm matches target */
	if (enter_spt_lock(mm, &flags) < 0)
		return;

	mmap_assert_locked(mm);

	vstart = max_t(unsigned long, vstart, kspt_start_vaddr(&kspt));
	vend = min_t(unsigned long, vend, kspt_end_vaddr(&kspt));

	if (vstart >= vend)
		goto EXIT_UNLOCK;

	/* use vlast to keep track of ranges we 'skip' (so, not in a vma) */
	vlast = vstart;
	for_each_vma_range(vmi, vma, vend) {
		__update_kspt_vma(mm, vma);

		kspt_mark_range_deallocated(vlast, vma->vm_start);

		vlast = vma->vm_end;
	}

	kspt_mark_range_deallocated(vlast, vend);

EXIT_UNLOCK:
	exit_spt_lock(flags);
}

/* PTE UPDATE FUNCTIONS */

/* Core function for updating a single virtual address from PTE. Walks page table, copies PTE
 * (if it exists), and populates provided dest_spte with appropriate values and flags. Must be
 * called with mm read lock then spt_lock in that order. Does no input checks.
 */
static void __update_kspt_pte(struct mm_struct *mm, unsigned long vaddr,
	struct shadow_pte *dest_spte)
{
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *ptep;
	spinlock_t *ptl;

	spte_set_vaddr(dest_spte, vaddr);

	mmap_assert_locked(mm);

	pgd = pgd_offset(mm, vaddr);
	if (pgd_none(*pgd) || pgd_bad(*pgd))
		goto NO_ENTRY;

	p4d = p4d_offset(pgd, vaddr);
	if (p4d_none(*p4d) || p4d_bad(*p4d))
		goto NO_ENTRY;

	pud = pud_offset(p4d, vaddr);
	if (pud_none(*pud))
		goto NO_ENTRY;

	pmd = pmd_offset(pud, vaddr);
	if (pmd_none(*pmd))
		goto NO_ENTRY;

	/* For huge pages only (not required) */
	if (pmd_trans_huge(*pmd)) {
		ptl = pmd_lock(mm, pmd);
		if (pmd_trans_huge(*pmd)) {
			update_spte_pmd(dest_spte, *pmd);
			spin_unlock(ptl);
			return;
		}
		spin_unlock(ptl);
	}

	ptep = pte_offset_map_lock(mm, pmd, vaddr, &ptl);
	if (!pte_none(*ptep)) {
		update_spte_pte(dest_spte, *ptep);
		pte_unmap_unlock(ptep, ptl);
		return;
	}
	pte_unmap_unlock(ptep, ptl);

NO_ENTRY:
	clear_spte_pte(dest_spte);
}


/* Update SPTE from single virtual address PTE. Does checks/locking and calls core function. Must
 * be called while holding mm read lock. Called from page fault handler.
 */
void update_kspt_pte(struct mm_struct *mm, unsigned long vaddr)
{
	struct shadow_pte *spte;
	unsigned long flags;

	/* Get lock if spt active and mm matches target */
	if (enter_spt_lock(mm, &flags) < 0)
		return;

	mmap_assert_locked(mm);

	spte = vaddr_to_kspte(&kspt, vaddr);
	if (!spte) {
		exit_spt_lock(flags);
		return;
	}

	__update_kspt_pte(mm, vaddr, spte);

	exit_spt_lock(flags);
}


/* Updates all SPTEs within provided vaddr range. Must be called while holding mm read lock.*/
void update_kspt_pte_range(struct mm_struct *mm, unsigned long vstart, unsigned long vend)
{
	unsigned long flags, vaddr;
	struct shadow_pte *spte;

	if (enter_spt_lock(mm, &flags) < 0)
		return;

	mmap_assert_locked(mm);

	// Coerce range to be contained within target range
	vstart = max_t(unsigned long, vstart, kspt_start_vaddr(&kspt));
	vend = min_t(unsigned long, vend, kspt_end_vaddr(&kspt));

	for (vaddr = vstart; vaddr < vend; vaddr += PAGE_SIZE) {
		spte = vaddr_to_kspte(&kspt, vaddr);

		WARN_ON(!spte);
		__update_kspt_pte(mm, vaddr, spte);
	}

	exit_spt_lock(flags);
}

/* MMU Notifier callback when PTEs are invalidated. Called holding mm read lock. */
static void shadowpt_invalidate_range_end(struct mmu_notifier *sub,
	const struct mmu_notifier_range *range)
{
	if (!is_kspt_active)
		return;

	update_kspt_pte_range(range->mm, range->start, range->end);
}

/* DISABLE/DEACTIVATE FUNCTIONS */

/*
 * Undo the remap_pfn_range operation by zapping the range. Must hold mm write lock.
 * Returns 0 on success, -1 on failure.
 */
static int unmap_spt_entries(struct mm_struct *inspector_mm,
	struct user_shadow_pt *old_user_spt, unsigned long vm_flags)
{
	unsigned long size, addr;
	struct vm_area_struct *mapped_vma;

	if (!inspector_mm)
		return -1;

	size = PAGE_ALIGN(spt_byte_size(old_user_spt));
	WARN_ON(size == 0);

	addr = (unsigned long)spt_entries(old_user_spt);

	mmap_write_lock(inspector_mm);

	mapped_vma = vma_lookup(inspector_mm, addr);
	if (!mapped_vma) {
		mmap_write_unlock(inspector_mm);
		return -1;
	}

	zap_vma_ptes(mapped_vma, addr, size);

	mapped_vma->vm_flags = vm_flags;
	mmap_write_unlock(inspector_mm);
	return 0;
}

/* Must be called while holding status mutex. Also called in mm_release */
void kspt_mm_release(struct mm_struct *target_mm)
{
	unsigned long flags, vstart, vend;
	struct mmu_notifier *notifier;

	if (!is_kspt_active || !is_kspt_target_mm(target_mm))
		return;

	if (!is_kspt_mm_active)
		return;

	/* Call spt lock to write new values to kspt */
	spin_lock_irqsave(&kspt.spt_lock, flags);

	WARN_ON(!kspt.spte_mmu_notifier);
	notifier = kspt.spte_mmu_notifier;
	kspt.spte_mmu_notifier = NULL;

	vstart = kspt_start_vaddr(&kspt);
	vend = kspt_end_vaddr(&kspt);

	WARN_ON_ONCE(!kspt.target_mm || kspt.target_mm != target_mm);
	WRITE_ONCE(kspt.target_mm, NULL);


	WARN_ON_ONCE(!kspt.target_mm_active);
	WRITE_ONCE(kspt.target_mm_active, 0);

	memset(kspt.kentries, 0, kspt.kentries_bytesize);

	spin_unlock_irqrestore(&kspt.spt_lock, flags);

	mmu_notifier_unregister(notifier, target_mm);
	kfree(notifier);
	mmput(target_mm);

}

/* Try to disable. Only succeeds if current process is inspector. Must be called while holding
 * status lock, but will return with status lock released.
 */
long do_shadowpt_disable_unlock(void)
{
	unsigned long flags, vma_flags;
	struct user_shadow_pt *old_user_spt;
	struct shadow_pte *old_kentries;
	struct task_struct *old_target;
	unsigned long old_kentries_bytesize;

	if (!is_kspt_active) {
		mutex_unlock(&kspt.status_lock);
		return -ESRCH;
	}

	/* Only the inspector can disable itself, either by calling syscall or while exiting */
	if (READ_ONCE(kspt.inspector) != current) {
		mutex_unlock(&kspt.status_lock);
		return -EPERM;
	}

	kspt_mm_release(READ_ONCE(kspt.target_mm));

	spin_lock_irqsave(&kspt.spt_lock, flags);

	WRITE_ONCE(kspt.active, false);

	mb(); /* likely unneeded */

	old_target = kspt.target;
	old_user_spt = kspt.user_shadow_pt;
	old_kentries = kspt.kentries;
	old_kentries_bytesize = kspt.kentries_bytesize;
	vma_flags = kspt.inspector_vma_flags;

	WRITE_ONCE(kspt.inspector, NULL);
	kspt.target = NULL;
	kspt.user_shadow_pt = NULL;
	kspt.kentries = NULL;
	kspt.kentries_bytesize = 0;
	kspt.inspector_vma_flags = 0;

	WARN_ON_ONCE(kspt.spte_mmu_notifier);
	WARN_ON_ONCE(kspt.target_mm);
	WARN_ON_ONCE(kspt.target_mm_active);

	spin_unlock_irqrestore(&kspt.spt_lock, flags);
	mutex_unlock(&kspt.status_lock);

	unmap_spt_entries(current->mm, old_user_spt, vma_flags);
	kfree(old_user_spt);

	free_pages_exact(old_kentries, old_kentries_bytesize);

	put_task_struct(old_target);

	return 0;

}


/* Called when any proc exits. Tries to disable, only succeeds if exiting proc is inspector. */
void check_exit_kspt(void)
{
	mutex_lock(&kspt.status_lock);
	do_shadowpt_disable_unlock();
}

SYSCALL_DEFINE0(shadowpt_disable)
{
	mutex_lock(&kspt.status_lock);
	return do_shadowpt_disable_unlock();
}


/* ENABLE/ACTIVATE FUNCTIONS */

static const struct mmu_notifier_ops shadowpt_mmu_notifier_ops = {
	.invalidate_range_end = shadowpt_invalidate_range_end
};

/* Register mmu notifier with mm struct. Need pinned mm struct. Can't hold mm lock or spinlock when
 * called. Returns 0 on success or -errno on failure.
 */
static int shadowpt_notifier_register(struct mm_struct *target_mm,
	struct mmu_notifier *dest_notifier)
{
	dest_notifier->ops = &shadowpt_mmu_notifier_ops;
	return mmu_notifier_register(dest_notifier, target_mm);
}


/* Core mm activate function, doesn't perform status checks. Must be called while holding status
 * mutex but not mm lock. Returns 1 on activated, -errno on failure.
 */
static long __kspt_mm_active(struct mm_struct *mm)
{
	unsigned long flags, vstart, vend;
	struct mmu_notifier *notifier;
	long err;

	notifier = kmalloc(sizeof(struct mmu_notifier), GFP_KERNEL);
	if (!notifier)
		return -ENOMEM;

	err = shadowpt_notifier_register(mm, notifier);
	if (err)
		goto FREE_NOTIFIER;

	/* Call spt lock to write new values to kspt */
	spin_lock_irqsave(&kspt.spt_lock, flags);

	WARN_ON(kspt.spte_mmu_notifier);
	kspt.spte_mmu_notifier = notifier;
	vstart = kspt_start_vaddr(&kspt);
	vend = kspt_end_vaddr(&kspt);

	WARN_ON_ONCE(kspt.target_mm);
	WRITE_ONCE(kspt.target_mm, mm);

	WARN_ON_ONCE(kspt.target_mm_active);
	WRITE_ONCE(kspt.target_mm_active, 1);

	spin_unlock_irqrestore(&kspt.spt_lock, flags);

	/* All callbacks setup, now do our first pass over full range */
	mmap_read_lock(mm);

	update_kspt_pte_range(mm, vstart, vend);
	update_kspt_vma_range(mm, vstart, vend);

	mmap_read_unlock(mm);

	return 1;

FREE_NOTIFIER:
	kfree(notifier);
	return err;

}

/* Wraps activating the inspected task's mm, but for use externally (exec). Calls kspt status lock,
 * pins mm, and checks return result, disabling on failure.
 */
void kspt_mm_try_activate(void)
{
	long ret;
	struct mm_struct *new_mm;

	/* Fast check so unrelated processes don't need to wait on mutex */
	if (!is_kspt_active || likely(!is_kspt_target_tsk(current)))
		return;

	mutex_lock(&kspt.status_lock);

	/* real check */
	if (!is_kspt_active || !is_kspt_target_tsk(current))
		goto UNLOCK_EXIT;

	if (is_kspt_mm_active)
		goto UNLOCK_EXIT;

	new_mm = get_task_mm(current);
	if (!new_mm)
		goto ERROR_EXIT;

	ret = __kspt_mm_active(new_mm);
	if (ret < 0)
		goto ERROR_EXIT;

UNLOCK_EXIT:
	mutex_unlock(&kspt.status_lock);
	return;
ERROR_EXIT:
	do_shadowpt_disable_unlock();
}

/* Activates mm struct in kspt, with status checks. Must hold status mutex but not mm lock, and mm
 * struct should already be pinned. Returns 0 on didn't activate, 1 on activated, -errno on error.
 */
static long kspt_mm_activate(struct mm_struct *mm)
{
	if (!is_kspt_active || is_kspt_mm_active)
		return 0;

	return __kspt_mm_active(mm);
}

/* Iterate over all PTEs in a vma, and return true if at least one exists */
static bool some_ptes_exist(struct vm_area_struct *vma)
{
	unsigned long curr;
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *ptep;

	for (curr = vma->vm_start; curr < vma->vm_end; curr += PAGE_SIZE) {
		pgd = pgd_offset(vma->vm_mm, curr);

		if (pgd_none(*pgd) || pgd_bad(*pgd))
			continue;

		p4d = p4d_offset(pgd, curr);
		if (p4d_none(*p4d) || p4d_bad(*p4d))
			continue;

		pud = pud_offset(p4d, curr);
		if (pud_none(*pud))
			continue;

		pmd = pmd_offset(pud, curr);
		if (pmd_none(*pmd))
			continue;

		/* only for huge pages (not required) */
		if (pmd_trans_unstable(pmd))
			return 1;

		ptep = pte_offset_map(pmd, curr);
		if (!pte_none(*ptep))
			return 1;

	}

	return 0;
}


/* Get and validate vm struct from address and range, to be used for shadowpt. Checks that address
 * range entirely contained in vma, write flag unset, no PTEs allocated, and if merged with other
 * VMAs tries to isolate with split_vma. Saves flags to be restored on unmap.
 */
static struct vm_area_struct *get_valid_vma_region(struct mm_struct *current_mm,
	unsigned long addr, unsigned long size, unsigned long *flags)
{
	struct vm_area_struct *vma = find_vma(current_mm, addr);

	if (!vma)
		return ERR_PTR(-EINVAL);

	if (vma->vm_end - addr < size)
		return ERR_PTR(-EINVAL);

	if (vma->vm_flags & VM_WRITE)
		return ERR_PTR(-EINVAL);

	if ((addr != vma->vm_start) && split_vma(current_mm, vma, addr, 1))
		return ERR_PTR(-EAGAIN);

	if ((addr + size != vma->vm_end) && split_vma(current_mm, vma, addr + size, 0))
		return ERR_PTR(-EAGAIN);

	if (some_ptes_exist(vma))
		return ERR_PTR(-EINVAL);

	/* Save so we can restore at the end */
	*flags = vma->vm_flags;

	vma->vm_flags &= ~VM_MAYWRITE;

	return vma;
}

/* Remap spt entries to userspace based on dest struct. Performs checks to ensure bad userspace
 * inputs don't mess things up.
 */
static long remap_spt_entries(struct shadow_pte *kentries,
	struct user_shadow_pt *dest, unsigned long *vma_flags)
{
	struct mm_struct *curr_mm;
	unsigned long size, addr, pfn;
	struct vm_area_struct *curr_vma;
	int ret;

	curr_mm = current->mm;
	if (!curr_mm)
		return -EINVAL;

	size = PAGE_ALIGN(spt_byte_size(dest));
	if (size == 0)
		return -EINVAL;

	addr = (unsigned long)spt_entries(dest);

	mmap_write_lock(curr_mm);

	curr_vma = get_valid_vma_region(curr_mm, addr, size, vma_flags);
	if (IS_ERR(curr_vma)) {
		mmap_write_unlock(curr_mm);
		return PTR_ERR(curr_vma);
	}

	pfn = virt_to_phys(kentries) >> PAGE_SHIFT;
	ret = remap_pfn_range(curr_vma, addr, pfn, size, curr_vma->vm_page_prot);

	mmap_write_unlock(curr_mm);
	return ret;
}


/* Get main thread task struct from pid. Must hold RCU lock to call. */
static struct task_struct *get_process(pid_t thread_pid)
{
	struct task_struct *p, *t;

	if (thread_pid == 0)
		return &init_task;

	for_each_process_thread(p, t) {
		if (task_pid_nr(t) == thread_pid) {
			get_task_struct(p); /* must later call put_task_struct */
			return p;
		}
	}

	return NULL;
}

/* Copies the provided user shadow page table struct into newly allocated kernel memory, coerces
 * range and alignment, and copies back to userspace. Returns pointer to kernelspace copy or
 * ERR_PTR(-ernno) on error.
 */
static struct user_shadow_pt *dup_user_spt(struct user_shadow_pt __user *uspt)
{
	struct user_shadow_pt *kuspt;
	int size, new_range_size;

	 kuspt = kmalloc(sizeof(struct user_shadow_pt), GFP_KERNEL);
	if (!kuspt)
		return ERR_PTR(-ENOMEM);

	size = sizeof(struct user_shadow_pt);
	if (copy_from_user(kuspt, uspt, size))
		goto EXIT_ERR;

	spt_page_align(kuspt);
	new_range_size = spt_reduce_size(kuspt);
	if (new_range_size <= 0 || new_range_size > MAX_SPT_RANGE)
		goto EXIT_ERR;

	if (copy_to_user(uspt, kuspt, size))
		goto EXIT_ERR;

	return kuspt;

EXIT_ERR:
	kfree(kuspt);
	return ERR_PTR(-EINVAL);
}

SYSCALL_DEFINE2(shadowpt_enable, pid_t, target, struct user_shadow_pt __user *, dest) {
	struct task_struct *target_task;
	struct mm_struct *target_mm;
	unsigned long vma_flags, flags, entries_bytesize;
	struct user_shadow_pt *kuspt;
	struct shadow_pte *kentries;
	long err;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	/* enter lock against other enable/disable calls */
	mutex_lock(&kspt.status_lock);

	if (is_kspt_active) {
		err = -EBUSY;
		goto RELEASE_STATUS_LOCK;
	}

	kuspt = dup_user_spt(dest);
	if (IS_ERR(kuspt)) {
		err = PTR_ERR(kuspt);
		goto RELEASE_STATUS_LOCK;
	}

	rcu_read_lock();

	target_task = get_process(target);
	if (!target_task) {
		rcu_read_unlock();
		err = -EINVAL;
		goto FREE_KUSPT;
	}

	target_mm = get_task_mm(target_task); /* must call mmput later */

	rcu_read_unlock();

	if (!target_mm) {
		err = -EINVAL;
		goto PUT_TARGET_TASK;
	}

	entries_bytesize = PAGE_ALIGN(spt_byte_size(kuspt));
	kentries = alloc_pages_exact(entries_bytesize, GFP_KERNEL);
	if (!kentries) {
		err = -ENOMEM;
		goto PUT_TARGET_TASK;
	}

	err = remap_spt_entries(kentries, kuspt, &vma_flags);
	if (err < 0)
		goto FREE_KENTRIES;

	/* lock against all kspt updates (including page faults) */
	spin_lock_irqsave(&kspt.spt_lock, flags);

	kspt.target = target_task;
	WRITE_ONCE(kspt.inspector, current);
	kspt.kentries = kentries;
	kspt.inspector_vma_flags = vma_flags;
	kspt.kentries_bytesize = entries_bytesize;
	kspt.user_shadow_pt = kuspt;

	/* Stuff that won't be populated until later, when we activate target_mm */
	kspt.spte_mmu_notifier = NULL;
	WRITE_ONCE(kspt.target_mm, NULL);
	WRITE_ONCE(kspt.target_mm_active, false);

	mb(); /* likely unneeded */

	WRITE_ONCE(kspt.active, true);
	spin_unlock_irqrestore(&kspt.spt_lock, flags);

	kspt_mm_activate(target_mm);

	mutex_unlock(&kspt.status_lock);
	return 0;

FREE_KENTRIES:
	free_pages_exact(kentries, entries_bytesize);
PUT_TARGET_TASK:
	put_task_struct(target_task);
FREE_KUSPT:
	kfree(kuspt);
RELEASE_STATUS_LOCK:
	mutex_unlock(&kspt.status_lock);
	return err;
}

static void copy_page_contents(void *output, unsigned long pfn)
{
	unsigned long paddr;
	void *contents;

	WARN_ON(!output);

	paddr = pfn << PAGE_SHIFT;
	contents = phys_to_virt(paddr);

	memmove(output, contents, PAGE_SIZE);
}

SYSCALL_DEFINE2(shadowpt_page_contents, unsigned long, pfn, void __user *, buffer)
{
	unsigned long flags, err, curr, curr_pfn;
	struct shadow_pte *spte;
	void *out_buff;
	bool found;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (!is_kspt_active || !is_kspt_mm_active)
		return -EINVAL;

	err = 0;

	out_buff = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!out_buff)
		return -ENOMEM;

	mutex_lock(&kspt.status_lock);
	spin_lock_irqsave(&kspt.spt_lock, flags);

	if (!is_kspt_active || !is_kspt_mm_active) {
		err = -EINVAL;
		goto UNLOCK_EXIT;
	}

	/* Iterate over shadow page table */
	found = false;
	for (curr = kspt_start_vaddr(&kspt); curr < kspt_end_vaddr(&kspt); curr += PAGE_SIZE) {
		spte = vaddr_to_kspte(&kspt, curr);

		if (!spte) {
			WARN_ON(1);
			err = -EAGAIN;
			goto UNLOCK_EXIT;
		}

		curr_pfn = spte_pfn(spte);
		if (pfn == curr_pfn) {
			found = true;
			break;
		}
	}

	if (pfn == 0 || !found) {
		err = -ENOENT;
		goto UNLOCK_EXIT;
	}

	copy_page_contents(out_buff, pfn);

	mutex_unlock(&kspt.status_lock);
	spin_unlock_irqrestore(&kspt.spt_lock, flags);

	if (copy_to_user(buffer, out_buff, PAGE_SIZE))
		return -EFAULT;

	return 0;

UNLOCK_EXIT:
	kfree(out_buff);
	mutex_unlock(&kspt.status_lock);
	spin_unlock_irqrestore(&kspt.spt_lock, flags);
	return err;
}