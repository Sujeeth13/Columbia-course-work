#ifndef _UAPI_SHADOWPT_H
#define _UAPI_SHADOWPT_H

#ifdef __KERNEL__
#include <asm/page.h>
#else
#include <unistd.h>
#define PAGE_SIZE getpagesize()
#endif

#include <linux/kernel.h>

#define NR_shadowpt_enable 451
#define NR_shadowpt_disable 452
#define NR_shadowpt_page_contents 453


/* The data structure representing a single entry in the page table */
struct shadow_pte {
	unsigned long vaddr; /* Virtual address of the base of this page */
	unsigned long pfn; /* Physical frame number of the assocated physical page */
	unsigned long state_flags; /* Flags describing the mapping state */
};

#define SPTE_VADDR_ALLOCATED 1    /* the page is allocated */
#define SPTE_VADDR_ANONYMOUS 2    /* the page is for anonymous memory */
#define SPTE_VADDR_WRITEABLE 4    /* the page is writable (else read only) */

#define SPTE_PTE_MAPPED 8         /* a physical frame of memory is mapped */
#define SPTE_PTE_WRITEABLE 16     /* the frame of memory is writable */

#define spte_pfn(spte) ((spte)->pfn)
#define spte_vaddr(spte) ((spte)->vaddr)
#define spte_flags(spte) ((spte)->state_flags)

/* Macros for testing whether each state flag is set */
#define is_vaddr_allocated(state) (state & SPTE_VADDR_ALLOCATED)
#define is_vaddr_anonymous(state) (state & SPTE_VADDR_ANONYMOUS)
#define is_vaddr_writeable(state) (state & SPTE_VADDR_WRITEABLE)

#define is_pte_mapped(state) (state & SPTE_PTE_MAPPED)
#define is_pte_writeable(state) (state & SPTE_PTE_WRITEABLE)


#define MAX_SPT_RANGE (8388608)

/* The data structure representing the full page table */
struct user_shadow_pt {
__u64 start_vaddr; /* first virtual address in the target range */
__u64 end_vaddr; /* last virtual address in the target range + 1 */
struct shadow_pte __user *entries; /* array of all page table entries */
};

#define SPTE_SIZE sizeof(struct shadow_pte)

#define spt_start_vaddr(spt) ((spt)->start_vaddr)
#define spt_end_vaddr(spt) ((spt)->end_vaddr)
#define spt_entries(spt) ((spt)->entries)

/* Calculate required bytes to store the entries of an spt with given range */
static inline __u64 spt_required_bytes(__u64 vaddr_range_size)
{
	return (vaddr_range_size/PAGE_SIZE) * SPTE_SIZE;
}

/* Returns the number of virtual addresses tracked by a shadow page table */
static inline __u64 spt_range_size(struct user_shadow_pt *spt)
{
	return (spt->end_vaddr - spt->start_vaddr);
}

/* Returns the number of bytes required by the given shadow page table */
static inline __u64 spt_byte_size(struct user_shadow_pt *spt)
{
	return spt_required_bytes(spt_range_size(spt));
}

/*
 * Reduces the end address of the given shadow page table to limit the range
 * to MAX_SPT_RANGE addresses. Returns the new range size.
 */
static inline int spt_reduce_size(struct user_shadow_pt *spt)
{
	spt->end_vaddr = min(spt->end_vaddr, spt->start_vaddr + MAX_SPT_RANGE);
	return spt_range_size(spt);
}

/* Page-aligns the start and end addresses down in the given shadow page table. */
static inline void spt_page_align(struct user_shadow_pt *spt)
{
	spt->start_vaddr = spt->start_vaddr & ~(PAGE_SIZE-1);
	spt->end_vaddr = spt->end_vaddr & ~(PAGE_SIZE-1);
}

/* Convert a virtual address to an index into the spt entries array */
static inline __u64 vaddr_to_spte_index(__u64 vaddr, __u64 start)
{
	return (vaddr - start) / PAGE_SIZE;
}

/* Convert an index into the spt entries array to its virtual address */
static inline __u64 pte_index_to_vaddr(__u64 spte_index, __u64 start)
{
	return start + (spte_index * PAGE_SIZE);
}

/* Convert a virtual address to a shadow_pte pointer, or return NULL if out of range */
static inline struct shadow_pte __user *vaddr_to_spte(__u64 vaddr, struct user_shadow_pt *spt)
{
	if (vaddr < spt_start_vaddr(spt) || vaddr > spt_end_vaddr(spt))
		return NULL;

	return &spt_entries(spt)[vaddr_to_spte_index(vaddr, spt_start_vaddr(spt))];
}

/* Initialize a shadowpt struct given an allocated mapping and target range */
static inline void INIT_SHADOW_PT(struct user_shadow_pt *spt, void *mapped,
	__u64 start_vaddr, __u64 end_vaddr)
{
	spt->entries = (struct shadow_pte *)mapped;
	spt->start_vaddr = start_vaddr;
	spt->end_vaddr = end_vaddr;
}

#endif /* _UAPI_SHADOWPT_H */
