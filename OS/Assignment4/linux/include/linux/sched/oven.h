#ifndef _SCHED_OVEN_H
#define _SCHED_OVEN_H

#define OVEN_MAX_WEIGHT 10000

static inline int oven_task(struct task_struct *p)
{
	if (likely(p->policy == SCHED_OVEN))
		return 1;
	return 0;
}

#endif /* _SCHED_OVEN_H */