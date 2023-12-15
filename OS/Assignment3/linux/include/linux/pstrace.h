#ifndef _LINUX_PSTRACE_H_
#define _LINUX_PSTRACE_H_

#include <linux/atomic.h>
#include <uapi/linux/pstrace.h>

#define PSTRACE_BUF_SIZE 500

struct pstrace_kernel {
	struct pstrace entry[PSTRACE_BUF_SIZE];
	bool empty;
	int head, tail;
	spinlock_t pstrace_lock;
	atomic_t counter;
};

struct pstrace_traced {
	atomic_t pid;
};

struct pstrace_evt {
	long counter;
	struct pstrace buf[PSTRACE_BUF_SIZE];
	bool woken;
	bool copied;
	wait_queue_head_t evt_waitq; // WARNING: this lock is used for
						 // all data fields, not just waitq
	int nr_entries;
	struct list_head head;
};

void pstrace_add(struct task_struct *p, long stateoption);
void wake_blocked_gets(void);

#endif /* _LINUX_PSTRACE_H_ */