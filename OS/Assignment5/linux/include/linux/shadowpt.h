#ifndef _SHADOWPT_H
#define _SHADOWPT_H

#include <uapi/linux/shadowpt.h>

#include <linux/kernel.h>
#include <linux/mmu_notifier.h>
#include <linux/sched.h>
#include <linux/pgtable.h>
#include <linux/mm.h>

#include <linux/mutex.h>
#include <linux/spinlock.h>

/* Kernel struct for shadow page table */
struct kern_shadow_pt {
	spinlock_t spt_lock; /* protects struct fields */
	struct mutex status_lock; /* protects activation status */

	bool active;
	struct task_struct *inspector;
	struct task_struct *target;

	bool target_mm_active;
	struct mm_struct *target_mm; /* to check without dereferencing target */

	struct user_shadow_pt *user_shadow_pt;

	unsigned long kentries_bytesize;
	struct shadow_pte *kentries;

	unsigned long inspector_vma_flags;
	struct mmu_notifier *spte_mmu_notifier;
};

#define kspt_start_vaddr(kern_spt) (spt_start_vaddr((kern_spt)->user_shadow_pt))
#define kspt_end_vaddr(kern_spt) (spt_end_vaddr((kern_spt)->user_shadow_pt))

/* Get kernel spte pointer from user vaddr, or NULL if out of range */
static inline struct shadow_pte *vaddr_to_kspte(struct kern_shadow_pt *kspt, unsigned long vaddr)
{
	unsigned long pte_index;

	if (vaddr < kspt_start_vaddr(kspt) || vaddr > kspt_end_vaddr(kspt))
		return NULL;

	pte_index = vaddr_to_spte_index(vaddr, kspt_start_vaddr(kspt));
	return kspt->kentries + pte_index;
}

/* VMA flag update functions */

static inline void spte_set_allocated(struct shadow_pte *spte, bool value)
{
	if (value)
		(spte->state_flags) |= SPTE_VADDR_ALLOCATED;
	else
		(spte->state_flags) &= ~SPTE_VADDR_ALLOCATED;
}

static inline void spte_set_anonymous(struct shadow_pte *spte, bool value)
{
	if (value)
		(spte->state_flags) |= SPTE_VADDR_ANONYMOUS;
	else
		(spte->state_flags) &= ~SPTE_VADDR_ANONYMOUS;
}


static inline void spte_set_vaddr_writeable(struct shadow_pte *spte, bool value)
{
	if (value)
		(spte->state_flags) |= SPTE_VADDR_WRITEABLE;
	else
		(spte->state_flags) &= ~SPTE_VADDR_WRITEABLE;
}


/* PTE flag update functions */

#define spte_set_pfn(spte, new_pfn) ((spte)->pfn = new_pfn)
#define spte_set_vaddr(spte, new_vaddr) ((spte)->vaddr = vaddr)

static inline void spte_set_pte_mapped(struct shadow_pte *spte, bool value)
{
	if (value)
		(spte->state_flags) |= SPTE_PTE_MAPPED;
	else
		(spte->state_flags) &= ~SPTE_PTE_MAPPED;
}

static inline void spte_set_pte_writeable(struct shadow_pte *spte, bool value)
{
	if (value)
		(spte->state_flags) |= SPTE_PTE_WRITEABLE;
	else
		(spte->state_flags) &= ~SPTE_PTE_WRITEABLE;
}

/* Updates spte based on provided pmd value. Only use for 2MB huge pages */
static inline void update_spte_pmd(struct shadow_pte *spte, pmd_t pmd)
{
	spte_set_pfn(spte, pmd_pfn(pmd));
	spte_set_pte_mapped(spte, true);
	spte_set_pte_writeable(spte, pmd_write(pmd));
}


/* Updates spte based on provided pte value */
static inline void update_spte_pte(struct shadow_pte *spte, pte_t pte)
{
	spte_set_pfn(spte, pte_pfn(pte));
	spte_set_pte_mapped(spte, true);
	spte_set_pte_writeable(spte, pte_write(pte));
}

/* Clears provided spte of physical mapping (indicating no PTE) */
static inline void clear_spte_pte(struct shadow_pte *spte)
{
	spte_set_pte_mapped(spte, false);
	spte_set_pfn(spte, 0);
}


/* Stuff that deals with global shadow pt, kspt (could be in separate header) */

extern struct kern_shadow_pt kspt;

/* Called when PTE changes */
extern void update_kspt_pte(struct mm_struct *mm, unsigned long vaddr);
extern void update_kspt_pte_range(struct mm_struct *mm, unsigned long vstart, unsigned long vend);

/* Called when VMA changes */
extern void update_kspt_vma(struct mm_struct *mm, struct vm_area_struct *vma);
extern void update_kspt_vma_range(struct mm_struct *mm, unsigned long vstart, unsigned long vend);

/* Called when process exits */
extern void check_exit_kspt(void);

/* Called when mm released (exit or exec) */
extern void kspt_mm_release(struct mm_struct *target_mm);
extern void kspt_mm_try_activate(void);

/* Check status of global kspt (safe even without holding spt lock) */
#define is_kspt_active READ_ONCE(kspt.active)
#define is_kspt_target_mm(mm) ((mm == READ_ONCE(kspt.target_mm)) && mm)
#define is_kspt_target_tsk(tsk) ((tsk == READ_ONCE(kspt.target)) && tsk)
#define is_kspt_mm_active READ_ONCE(kspt.target_mm_active)


#endif /* _SHADOWPT_H */
