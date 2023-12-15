#ifndef LINUX_SHADOWPT_H
#define LINUX_SHADOWPT_H

#include <uapi/linux/shadowpt.h>
#include <linux/types.h>


extern struct shadow_pte *shadow_table;
extern size_t shadow_page_table_size;
extern size_t num_pages;
extern unsigned long start_vaddr;
extern unsigned long end_vaddr;
extern pid_t target_pid;

bool is_inspector(pid_t curr);
bool is_target(pid_t curr);


/*
 * Syscall No. 451
 * Set up the shadow page table in kernelspace,
 * and remaps the memory for that pagetable into the indicated userspace range.
 * target should be a process, not a thread.
 */
long shadowpt_enable(pid_t target, struct user_shadow_pt *dest);

/*
 * Syscall No. 452
 * Release the shadow page table and clean up.
 */
long shadowpt_disable(void);
void shadow_table_init(void);
void update_shadow_vaddr_init(int set);
void update_shadow_vaddr(struct mm_struct *mm, struct vm_area_struct *vma, unsigned long state, int set);
void update_shadowpte(struct mm_struct *mm, unsigned long vaddr, unsigned long state, unsigned long pfn, int set);
void update_shadow_vaddr_GROWS(struct mm_struct *mm, struct vm_area_struct *vma, unsigned long oldStart);

#endif /* LINUX_SHADOW_PT_H */
