#ifndef UAPI_LINUX_SHADOWPT_H
#define UAPI_LINUX_SHADOWPT_H

/** A single entry in the page table */
struct shadow_pte {
	/** Virtual address of the base of this page */
	unsigned long vaddr;
	/** Physical frame number of the assocated physical page */
	unsigned long pfn;
	/** Flags describing the mapping state */
	unsigned long state_flags;
};

/** the page is allocated */
#define SPTE_VADDR_ALLOCATED	1
/** the page is for anonymous memory */
#define SPTE_VADDR_ANONYMOUS	2
/** the page is writable (else read only) */
#define SPTE_VADDR_WRITEABLE	4

/** a physical frame of memory is mapped */
#define SPTE_PTE_MAPPED		8
/** the frame of memory is writable */
#define SPTE_PTE_WRITEABLE	16

/* Macros for testing whether each state flag is set */
#define is_vaddr_allocated(state) (state & SPTE_VADDR_ALLOCATED)
#define is_vaddr_anonymous(state) (state & SPTE_VADDR_ANONYMOUS)
#define is_vaddr_writeable(state) (state & SPTE_VADDR_WRITEABLE)

#define is_pte_mapped(state) (state & SPTE_PTE_MAPPED)
#define is_pte_writeable(state) (state & SPTE_PTE_WRITEABLE)

#define MAX_SPT_RANGE (8388608)

/** the full page table */
struct user_shadow_pt {
	/** first virtual address in the target range */
	unsigned long start_vaddr;
	/** last virtual address in the target range + 1 */
	unsigned long end_vaddr;
	/** array of all page table entries */
	struct shadow_pte __user *entries;
};

#endif /* UAPI_LINUX_SHADOWPT_H */
