#include <uapi/linux/shadowpt.h>

#include <linux/mm.h>
#include <linux/pid.h>
#include <linux/shadowpt.h>
#include <linux/syscalls.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/gfp.h>
#include <linux/sched.h>
#include <linux/mmu_notifier.h>

#include <asm-generic/io.h>

#define pfn_to_virt(pfn)	__va((pfn) << PAGE_SHIFT)

/* comment this to disable debug messages */
// #define SHADOWPT_DEBUG

/**
 * Only one process can be inspecting another at a time. If a process calls the
 * system call while another process is using it, they should receive EBUSY.
 */
atomic_t busy = ATOMIC_INIT(false);
/**
 * the shadow page table
 */
struct shadow_pte *shadow_table;
/**
 * a copy of the @dest parameter that the inspector passed to shadowpt_enable
 *
 * it is copied because it is passed as a __user pointer
 */
struct user_shadow_pt dest_copy = {0, 0, NULL};
/**
 * the vma of the inspector's pointer to the shadow page table
 */
struct vm_area_struct *vma;
/**
 * the pid of the inspector -t he process that called `shadowpt_enable`
 */
pid_t inspector_pid;
/**
 * the pid of the process that the inspector is inspecting - passed as @target
 * in `shadowpt_enable`
 */
pid_t target_pid;
/**
 * the size of the shadow page table in pages
 */
size_t num_pages;

unsigned long start_vaddr;
unsigned long end_vaddr;


/**
 * the size of the shadow page table in bytes
 */
size_t spt_size(void)
{
	return num_pages * sizeof(struct shadow_pte);
}

/**
 * Resets all global state to its default values
 */
void reset_global_state(void)
{
	if (shadow_table) {
		free_pages_exact(shadow_table, spt_size());
		shadow_table = NULL;
	}

	dest_copy = (struct user_shadow_pt){0, 0, NULL};
	num_pages = 0;
	vma = NULL;
	inspector_pid = target_pid = -1;
	atomic_set(&busy, false);
}

bool pid_exists(pid_t pid)
{
	struct pid *pid_struct;
	struct task_struct *task;

	pid_struct = find_get_pid(pid);
	task = pid_task(pid_struct, PIDTYPE_PID);
	put_pid(pid_struct);

	return task != NULL;
}

bool valid_pid(pid_t pid)
{
	if (pid < 0 || pid > pid_max)
		return false;

	if (!pid_exists(pid))
		return false;

	return true;
}

bool valid_vaddr(unsigned long addr)
{
	return start_vaddr <= addr && addr <= end_vaddr;
}

bool is_inspector(pid_t curr)
{
	return curr == inspector_pid;
}

bool is_target(pid_t curr)
{
	return curr == target_pid;
}
void free_memory(struct shadow_pte *shadow_table)
{
	kfree(shadow_table);
}

int page_table_key(unsigned long addr, unsigned long start)
{
	size_t key;
	// addr = ALIGN_DOWN(addr,PAGE_SIZE);
	key = (addr - start)/PAGE_SIZE;
	return key;
}

unsigned long vaddr_to_pfn(struct mm_struct *mm, unsigned long vaddr)
{
	/**
	 * Walk the page table of the process and find the
	 * appropriate PTE entry for the vaddr
	*/
	unsigned long pfn;
	spinlock_t *ptl;
	pte_t *pte;
	int ret;

	ret = follow_pte(mm, vaddr, &pte, &ptl);

	if (ret)
		return 0;

	pfn = pte_pfn(*pte);
	pte_unmap_unlock(pte, ptl);
	return pfn;
}

pte_t *walk_the_table(struct mm_struct *mm, unsigned long vaddr)
{
	/**
	 * Walk the page table of the process and find the
	 * appropriate PTE entry for the vaddr
	*/
	spinlock_t *ptl;
	pte_t *pte;
	int ret;

	ret = follow_pte(mm, vaddr, &pte, &ptl);

	if (ret)
		return NULL;

	// pfn = pte_pfn(*pte);
	pte_unmap_unlock(pte, ptl);
	return pte;
}

static void page_invalidation_callback(struct mmu_notifier *mn,
			     const struct mmu_notifier_range *range)
{
    unsigned long vaddr;
	for (vaddr = range->start; vaddr <= range->end; vaddr = vaddr + PAGE_SIZE) {
		size_t entry;
		pte_t *pte;
		if (!valid_vaddr(vaddr))
			continue;
		entry = page_table_key(vaddr, start_vaddr);
		printk(KERN_INFO "Invalidating the page");
		pte = walk_the_table(range->mm, vaddr);
		if (pte) {
			if (pte_write(*pte)) {
				shadow_table[entry].pfn = pte_pfn(*pte);
				shadow_table[entry].state_flags |= (SPTE_PTE_MAPPED | SPTE_PTE_WRITEABLE);
			} else {
				shadow_table[entry].pfn = pte_pfn(*pte);
				shadow_table[entry].state_flags |= (SPTE_PTE_MAPPED);
				shadow_table[entry].state_flags &= ~(SPTE_PTE_WRITEABLE);
			}
		} else {
			shadow_table[entry].pfn = 0;
			shadow_table[entry].state_flags &= ~(SPTE_PTE_MAPPED | SPTE_PTE_WRITEABLE);
		}
	}
}

static const struct mmu_notifier_ops shadow_table_mmu_notifier_ops = {
    .invalidate_range_end = page_invalidation_callback,
	// .release = NULL,
	// .clear_flush_young = NULL,
	// .clear_young = NULL,
	// .test_young = NULL,
	// .change_pte = NULL,
	// .invalidate_range_start = NULL,
	// .invalidate_range_end = NULL,
	// .alloc_notifier = NULL,
	// .free_notifier = NULL,
};

struct mmu_notifier shadow_table_mmu_notifier = {
    .ops = &shadow_table_mmu_notifier_ops,
};

void shadow_table_init(void)
{
	unsigned long vaddr;
	size_t entry;
	struct task_struct *target_task;
	struct pid *pid_struct;
	int ret;

	pid_struct = find_get_pid(target_pid);
	target_task = pid_task(pid_struct, PIDTYPE_PID);
	put_pid(pid_struct);
	vaddr = start_vaddr;

	if (target_task) {
		ret = mmu_notifier_register(&shadow_table_mmu_notifier, target_task->mm);
		if (ret) {
			printk(KERN_INFO "Unable to register the MMU Notifier");
			return;
		}
	} else {
		printk(KERN_INFO "Could not find target process to register the MMU Notifier");
		return;
	}

	printk(KERN_INFO "update shadowpt_table line %d\n", __LINE__);
	for (entry = 0; entry < num_pages; entry++) {
		shadow_table[entry].vaddr = vaddr;
		shadow_table[entry].state_flags = 0;
		if (target_task) { /*init the pfn of target process*/
			shadow_table[entry].pfn = vaddr_to_pfn(target_task->mm, vaddr);
		}
		vaddr = vaddr + PAGE_SIZE;
	}
}

void update_shadow_vaddr_init(int set)
{
	unsigned long vaddr;
	size_t entry;
	struct task_struct *target_task;
	struct pid *pid_struct;

	if (!shadow_table)
		return;
	/*only update the shadow page table if it is tracking process or on init*/
	if (current->pid != target_pid || current->pid == 0)
		return;

	pid_struct = find_get_pid(target_pid);
	target_task = pid_task(pid_struct, PIDTYPE_PID);
	put_pid(pid_struct);
	vaddr = start_vaddr;

	printk(KERN_INFO "update shadowpt_table line %d\n", __LINE__);
	for (entry = 0; entry < num_pages; entry++) {
		shadow_table[entry].vaddr = vaddr;
		shadow_table[entry].state_flags = 0;
		if (target_task) { /*init the pfn of target process*/
			if (set)
				shadow_table[entry].pfn = vaddr_to_pfn(target_task->mm, vaddr);
			else
				shadow_table[entry].pfn = 0;
		}
		vaddr = vaddr + PAGE_SIZE;
	}
}
/**
 * This function tracks and makes the appropriate changes in the
 * shadow page table whenever there is a change in the virtual
 * address space.
 * KEY NOTE: ALWYAS CALL THIS FUNCTION WITH mmap_write_lock()
*/
void update_shadow_vaddr(struct mm_struct *mm, struct vm_area_struct *vma, unsigned long state, int set)
{
	/*Dont do anything if shadow page table hasn't been initialized*/
	if (!shadow_table)
		return;
	/*only update the shadow page table if it is tracking process or on init*/
	if (current->pid != target_pid)
		return;
	if (vma) { /*updating the shadow page table based on vaddr state change*/
		printk(KERN_INFO "update shadow_table line %d\n", __LINE__);
		for (unsigned long vaddr_base = vma->vm_start ; vaddr_base <= vma->vm_end; vaddr_base = vaddr_base + PAGE_SIZE) {
			size_t key;

			/* dont change the shadow table if the vaddr is not in the region we are tracking*/
			if (!valid_vaddr(vaddr_base)) {
				continue;
			}

			key = page_table_key(vaddr_base, start_vaddr);
			if (set)
				shadow_table[key].state_flags |= state;
			else
				shadow_table[key].state_flags &= ~(state);
		}
		printk(KERN_INFO "update shadow_table line %d\n", __LINE__);
	}
}

void update_shadow_vaddr_GROWS(struct mm_struct *mm, struct vm_area_struct *vma, unsigned long oldStart)
{
	/*Dont do anything if shadow page table hasn't been initialized*/
	if (!shadow_table)
		return;
	/*only update the shadow page table if it is tracking process or on init*/
	if (current->pid != target_pid)
		return;
	if (vma) { /*updating the shadow page table based on vaddr state change*/
		size_t key;
		unsigned long state;
		printk(KERN_INFO "update shadow_table GROWS line %d\n", __LINE__);
		key = page_table_key(oldStart, start_vaddr);
		state = shadow_table[key].state_flags;
		for (unsigned long vaddr_base = vma->vm_start ; vaddr_base <= vma->vm_end; vaddr_base = vaddr_base + PAGE_SIZE) {

			/* dont change the shadow table if the vaddr is not in the region we are tracking*/
			if (!valid_vaddr(vaddr_base)) {
				continue;
			}

			key = page_table_key(vaddr_base, start_vaddr);

			shadow_table[key].state_flags |= state;
		}
		printk(KERN_INFO "update shadow_table GROWS line %d\n", __LINE__);
	}
}

void update_shadowpte(struct mm_struct *mm, unsigned long vaddr, unsigned long state, unsigned long pfn, int set)
{
	unsigned long key;
	/*Dont do anything if shadow page table hasn't been initialized*/
	if (!shadow_table)
		return;
	/*only update the shadow page table if it is tracking process or on init*/
	if (current->pid != target_pid)
		return;
	/* dont change the shadow table if the vaddr is not in the region we are tracking*/
	if (!valid_vaddr(vaddr)) {
		return;
	}

	key = page_table_key(vaddr, start_vaddr);
	if (set) {
		if (state == SPTE_PTE_MAPPED) {
			shadow_table[key].pfn = pfn;
		}
		shadow_table[key].state_flags |= state;
	} else {
		if (state == SPTE_PTE_MAPPED)
			shadow_table[key].pfn = 0;
		shadow_table[key].state_flags &= ~(state);
	}

}
/**
 * Syscall No. 451
 * Set up the shadow page table in kernelspace,
 * and remaps the memory for that pagetable into the indicated userspace range.
 * target should be a process, not a thread.
 */
long shadowpt_enable(pid_t target, struct user_shadow_pt __user *dest)
{
	long retval;
	bool page_misaligned;

	size_t shadow_page_table_size;

	struct mm_struct *mm;
	int err;

#ifdef SHADOWPT_DEBUG
	printk(KERN_INFO "%s(@target=%d, @dest=%px)", __func__, target, dest);
#endif

	/*
	 * Only one process can be inspecting another at a time
	 *
	 * atomic_xchg returns the previous value of busy
	 */
	if (atomic_xchg(&busy, true)) {
#ifdef SHADOWPT_DEBUG
		printk(KERN_ERR "%s#L%d: busy", __func__, __LINE__);
#endif
		return -EBUSY;
	}

	/*
	 * From this point on, use `goto exit` or `goto fail` instead of
	 * `return`
	 */
	retval = 0;

	if (dest == NULL) {
#ifdef SHADOWPT_DEBUG
		printk(KERN_ERR "%s#L%d: @dest is NULL", __func__, __LINE__);
#endif
		retval = -EINVAL;
		goto fail;
	}

	if (copy_from_user(&dest_copy, dest, sizeof(struct user_shadow_pt))) {
		/* couldn't copy all the data from userspace. */
#ifdef SHADOWPT_DEBUG
		printk(KERN_ERR "%s#L%d: copy_from_user failed", __func__,
		       __LINE__);
#endif
		retval = -EFAULT;
		goto fail;
	}

#ifdef SHADOWPT_DEBUG
	printk(KERN_INFO "%s#L%d:\t@dest = struct user_shadow_pt {", __func__,
	       __LINE__);
	printk(KERN_INFO "%s#L%d:\t\t.start_vaddr = %lx,", __func__, __LINE__,
	       dest_copy.start_vaddr);
	printk(KERN_INFO "%s#L%d:\t\t.end_vaddr = %lx,", __func__, __LINE__,
	       dest_copy.end_vaddr);
	printk(KERN_INFO "%s#L%d:\t\t.entries = %px", __func__, __LINE__,
	       dest_copy.entries);
	printk(KERN_INFO "%s#L%d:\t}", __func__, __LINE__);
#endif

	if (!valid_pid(target)) {
#ifdef SHADOWPT_DEBUG
		printk(KERN_ERR "%s#L%d: invalid pid %d", __func__, __LINE__,
		       target);
#endif
		retval = -EINVAL;
		goto fail;
	}

	if (dest_copy.start_vaddr > dest_copy.end_vaddr) {
#ifdef SHADOWPT_DEBUG
		printk(KERN_ERR "%s#L%d: @dest->start (%lx) > @dest->end (%lx)",
		       __func__, __LINE__, dest_copy.start_vaddr,
		       dest_copy.end_vaddr);
#endif
		retval = -EINVAL;
		goto fail;
	}

	if (dest_copy.end_vaddr - dest_copy.start_vaddr < PAGE_SIZE) {
#ifdef SHADOWPT_DEBUG
		printk(KERN_ERR "%s#L%d: range between @dest->start (%lx) and "
				"@dest->end (%lx) is < PAGE_SIZE (%lx)",
		       __func__, __LINE__, dest_copy.start_vaddr,
		       dest_copy.end_vaddr, PAGE_SIZE);
#endif
		retval = -EINVAL;
		goto fail;
	}

	if (dest_copy.entries == NULL) {
#ifdef SHADOWPT_DEBUG
		printk(KERN_ERR "%s#L%d: @dest->entries is NULL", __func__,
		       __LINE__);
#endif
		retval = -EINVAL;
		goto fail;
	}

	if (!PAGE_ALIGNED(dest_copy.entries)) {
#ifdef SHADOWPT_DEBUG
		printk(KERN_ERR "%s#L%d: @dest->entries (%lx) is not page "
				"aligned (page size is %lx)",
		       __func__, __LINE__, (unsigned long)dest_copy.entries,
		       PAGE_SIZE);
#endif
		retval = -EINVAL;
		goto fail;
	}

	/*
	 * Only a superuser should be able to make the system call
	 */
	if (!capable(CAP_SYS_ADMIN)) {
#ifdef SHADOWPT_DEBUG
		printk(KERN_ERR "%s#L%d: not superuser", __func__, __LINE__);
#endif
		retval = -EPERM;
		goto fail;
	}

	/*
	 * The start address and end address + 1 should be page aligned. If they
	 * are not, the system call should shift them down to the nearest
	 * aligned values.
	 *
	 * The target range should not span more than MAX_SPT_RANGE virtual
	 * addresses. If it does, the system call should reduce the range to be
	 * MAX_SPT_RANGE.
	 *
	 * If either of the previous two happen, an updated user_shadow_pt
	 * struct with the new range values should be copied back to the
	 * userspace pointer.
	 */

	/**
	 * If any of the above conditions are true, we will set page_misaligned
	 * to true, and copy the updated user_shadow_pt struct back to userspace
	 * after fixing the alignment.
	 */
	page_misaligned = false;

	if (dest_copy.end_vaddr - dest_copy.start_vaddr > MAX_SPT_RANGE) {
#ifdef SHADOWPT_DEBUG
		printk(KERN_INFO "%s#L%d: range between @dest->start (%lx) and "
				 "@dest->end (%lx) is %lx, which is > "
				 "MAX_SPT_RANGE (%x). Shunting @dest->end "
				 "down...",
		       __func__, __LINE__, dest_copy.start_vaddr,
		       dest_copy.end_vaddr,
		       dest_copy.end_vaddr - dest_copy.start_vaddr,
		       MAX_SPT_RANGE);
#endif

		page_misaligned = true;
		dest_copy.end_vaddr = dest_copy.start_vaddr + MAX_SPT_RANGE;

#ifdef SHADOWPT_DEBUG
		printk(KERN_INFO "%s#L%d: ... @dest->end realigned to %lx, "
				 "which yields a range of %lx",
		       __func__, __LINE__, dest_copy.end_vaddr,
		       dest_copy.end_vaddr - dest_copy.start_vaddr);
#endif
	}

	if (!IS_ALIGNED(dest_copy.start_vaddr, PAGE_SIZE)) {
#ifdef SHADOWPT_DEBUG
		printk(KERN_INFO "%s#L%d: @dest->start (%lx) not aligned to "
				 "PAGE_SIZE (%lx)...",
		       __func__, __LINE__, dest_copy.start_vaddr, PAGE_SIZE);
#endif

		page_misaligned = true;
		dest_copy.start_vaddr =
			ALIGN_DOWN(dest_copy.start_vaddr, PAGE_SIZE);

#ifdef SHADOWPT_DEBUG
		printk(KERN_INFO "%s#L%d: ... @dest->start realigned to %lx",
		       __func__, __LINE__, dest_copy.start_vaddr);
#endif
	}

	if (!IS_ALIGNED(dest_copy.end_vaddr, PAGE_SIZE)) {
#ifdef SHADOWPT_DEBUG
		printk(KERN_INFO "%s#L%d: @dest->end (%lx) not aligned to "
				 "PAGE_SIZE (%lx)...",
		       __func__, __LINE__, dest_copy.end_vaddr, PAGE_SIZE);
#endif

		page_misaligned = true;
		dest_copy.end_vaddr =
			ALIGN_DOWN(dest_copy.end_vaddr, PAGE_SIZE);

#ifdef SHADOWPT_DEBUG
		printk(KERN_INFO "%s#L%d: ... @dest->end realigned to %lx",
		       __func__, __LINE__, dest_copy.end_vaddr);
#endif
	}

	if (page_misaligned) {
#ifdef SHADOWPT_DEBUG
		printk(KERN_INFO "%s#L%d: copying updated user_shadow_pt "
				 "struct back to userspace",
		       __func__, __LINE__);
#endif
		if (copy_to_user(dest, &dest_copy,
				 sizeof(struct user_shadow_pt))) {
#ifdef SHADOWPT_DEBUG
			printk(KERN_ERR "%s#L%d: copy_to_user failed", __func__,
			       __LINE__);
#endif
			retval = -EFAULT;
			goto fail;
		}
	}

#ifdef SHADOWPT_DEBUG
	printk(KERN_INFO "%s#L%d: building shadow page table", __func__,
	       __LINE__);
#endif

	start_vaddr = dest_copy.start_vaddr;
	end_vaddr = dest_copy.end_vaddr;
	num_pages = (end_vaddr - start_vaddr) / PAGE_SIZE;
	shadow_page_table_size = num_pages * sizeof(struct shadow_pte);

	shadow_table = (struct shadow_pte *)alloc_pages_exact(num_pages * sizeof(struct shadow_pte), GFP_KERNEL);
	if (!shadow_table) {
#ifdef SHADOWPT_DEBUG
		printk(KERN_ERR "%s#L%d: failed to allocate memory for shadow "
				"page table",
		       __func__, __LINE__);
#endif
		retval = -ENOMEM;
		goto fail;
	}

	mmap_write_lock(current->mm);
	shadow_table_init();
	mmap_write_unlock(current->mm);

#ifdef SHADOWPT_DEBUG
	printk(KERN_INFO "%s#L%d: remapping shadow page table into inspector's "
			 "virtual memory",
	       __func__, __LINE__);
#endif

	/* we need the vma that encompasses inspector's entries pointer */
	mm = current->mm;
	mmap_write_lock(mm);

	/* Finding the vma associated with the virtual address space*/
	vma = vma_lookup(mm, (unsigned long)dest_copy.entries);
	if (!vma) {
#ifdef SHADOWPT_DEBUG
		printk(KERN_ERR "%s#L%d: failed to find vma for inspector's "
				"entries pointer",
		       __func__, __LINE__);
#endif
		retval = -EFAULT;
		goto fail_with_mmap_lock;
	}

	/* if our address space doesn't begin at the beginning of the vma then split to the right*/
	if ((unsigned long)vma->vm_start != (unsigned long)dest_copy.entries) {
#ifdef SHADOWPT_DEBUG
		printk(KERN_INFO "%s#L%d: vma->vm_start (%lx)  != "
				 "@dest->entries (%lx), splitting vma.",
		       __func__, __LINE__, (unsigned long)vma->vm_start,
		       (unsigned long)dest_copy.entries);
#endif
		err = split_vma(mm, vma,
				PAGE_ALIGN((unsigned long)dest_copy.entries),
				1);
		if (err) {
#ifdef SHADOWPT_DEBUG
			printk(KERN_ERR "%s#L%d: failed to split vma for "
					"@dest->entries",
			       __func__, __LINE__);
#endif
			retval = err;
			goto fail_with_mmap_lock;
		}
	}

	/* if our address space doesn't end at the ending of the vma then split to the left*/
	err = split_vma(mm, vma,
			PAGE_ALIGN((unsigned long)dest_copy.entries +
				   shadow_page_table_size),
			0);

	if (err) {
#ifdef SHADOWPT_DEBUG
		printk(KERN_ERR "%s#L%d: failed to split vma for "
				"@dest->entries + shadow_page_table_size (%zu)",
		       __func__, __LINE__, shadow_page_table_size);
#endif
		retval = err;
		goto fail_with_mmap_lock;
	}

	/*
	 * remap the pages storing the shadow page table into the inspector's
	 * virtual memory
	 */



	zap_vma_ptes(vma, vma->vm_start, vma->vm_end-vma->vm_start);

	err = remap_pfn_range(vma, (unsigned long) dest_copy.entries,
			virt_to_phys(shadow_table) >> PAGE_SHIFT,
			num_pages * sizeof(struct shadow_pte), vma->vm_page_prot);

	mmap_write_unlock(mm);
	if (err) {
#ifdef SHADOWPT_DEBUG
		printk(KERN_ERR "%s#L%d: failed to remap shadow page table "
				"into inspector's virtual memory",
		       __func__, __LINE__);
#endif
		retval = err;
		goto fail;
	}


	/*
	 * At this point, the entries pointer provided by the userspace process
	 * should map to the same physical memory as the shadow page table
	 * created by the kernel
	 */
	target_pid = target;
	inspector_pid = current->pid;
	goto exit;

fail_with_mmap_lock:
	mmap_write_unlock(mm);
fail:
	reset_global_state();
exit:
#ifdef SHADOWPT_DEBUG
	printk(KERN_INFO "%s exiting\n", __func__);
#endif
	return retval;
}

/**
 * Syscall No. 452
 * Release the shadow page table and clean up.
 */
long shadowpt_disable(void)
{
	struct mm_struct *mm;

#ifdef SHADOWPT_DEBUG
	printk(KERN_INFO "%s()", __func__);
#endif

	if (shadow_table == NULL) {
#ifdef SHADOWPT_DEBUG
		printk(KERN_ERR "%s#L%d: called %s(), but there is no "
				"shadow page table at the moment",
				__func__, __LINE__, __func__);
#endif
		return -EPERM;
	}

	if (current->pid != inspector_pid && current->pid != target_pid) {
#ifdef SHADOWPT_DEBUG
		printk(KERN_ERR "%s#L%d: calling shadowpt_disable from a task "
				"other than the inspector",
				__func__, __LINE__);
#endif
		return -EACCES;
	}

	if (!atomic_read(&busy)) {
#ifdef SHADOWPT_DEBUG
		printk(KERN_ERR "%s#L%d: called %s(), but there is no "
				"inspector at the moment",
				__func__, __LINE__, __func__);
#endif
		return -EINVAL;
	}
	mm = current->mm;
	mmap_write_lock(mm);

	zap_vma_ptes(vma, vma->vm_start, vma->vm_end-vma->vm_start);

	mmap_write_unlock(mm);

	/* reset & free all global vars */
	reset_global_state();

#ifdef SHADOWPT_DEBUG
	printk("%s exiting", __func__);
#endif
	return 0;
}

/*
 * Syscall No. 453
 * Retrieve the contents of memory at a particular physical page
 */
long shadowpt_page_contents(unsigned long pfn, void __user *buffer)
{

	void *virtual_address;


	if (!capable(CAP_SYS_ADMIN)) {
		printk(KERN_ERR "%s#L%d: not superuser", __func__, __LINE__);
		return -EPERM;
	}

	if (shadow_table == NULL) {
		return -EPERM;
	}

	for (int i = 0; i < num_pages; i++) {
		if (shadow_table[i].pfn == pfn) {
			if (is_pte_mapped(shadow_table[i].state_flags)) {

				virtual_address = pfn_to_virt(pfn);

				if (copy_to_user(buffer, virtual_address, PAGE_SIZE)) {
					return -EFAULT;
				}
			}
		}
	}

	return 0;
}

SYSCALL_DEFINE2(shadowpt_enable, pid_t, target, struct user_shadow_pt *, dest)
{
	return shadowpt_enable(target, dest);
}

SYSCALL_DEFINE0(shadowpt_disable) {
	return shadowpt_disable();
}

SYSCALL_DEFINE2(shadowpt_page_contents, unsigned long, pfn, void __user *, buffer)
{
	return shadowpt_page_contents(pfn, buffer);
}
