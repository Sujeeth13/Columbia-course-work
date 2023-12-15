#include <linux/sched/cputime.h>
#include "sched.h"

#include <linux/sched/oven.h>
#include <linux/percpu.h>
#include <linux/timekeeping.h>

/* Initialize the oven rq */
void init_oven_rq(struct oven_rq *oven_rq)
{
	INIT_LIST_HEAD(&oven_rq->oven_rq_list);
	oven_rq->oven_nr_running = 0;
}

/* Some useful helper functions */
static inline struct task_struct *oven_task_of(struct sched_oven_entity *oven_se)
{
	return container_of(oven_se, struct task_struct, oven);
}

static inline struct rq *oven_rq_of(struct oven_rq *oven_rq)
{
	return container_of(oven_rq, struct rq, oven);
}

static inline struct oven_rq *oven_rq_of_oven_se(struct sched_oven_entity *oven_se)
{
	struct rq *rq = task_rq(oven_task_of(oven_se));

	return &rq->oven;
}

/* Calculate how many tasks on oven_rq need to run before oven_se can run */
static int
calc_oven_rq_num_running_before(
	struct oven_rq *oven_rq,
	struct sched_oven_entity *oven_se)
{
	struct sched_oven_entity *se;
	int before = 0;

	list_for_each_entry(se, &oven_rq->oven_rq_list, entity_list) {
		if (oven_se->weight <= se->weight)
			break;
		before++;
	}

	return before;
}

#ifdef CONFIG_SMP

/* Find the minimum weight of a task on oven_rq that's not running */
static int calc_oven_rq_min_weight_not_running(struct oven_rq *oven_rq)
{
	struct sched_oven_entity *oven_se;
	struct rq *rq = oven_rq_of(oven_rq);
	struct task_struct *p;

	list_for_each_entry(oven_se, &oven_rq->oven_rq_list, entity_list) {
		p = oven_task_of(oven_se);
		if (!task_on_cpu(rq, p))
			return p->oven.weight;
	}

	return INT_MAX;
}

/* Find a task on a cpu to move to rq */
static struct task_struct *pick_pushable_oven_task(struct rq *rq, int cpu)
{
	struct sched_oven_entity *oven_se;

	list_for_each_entry(oven_se, &rq->oven.oven_rq_list, entity_list) {
		struct task_struct *p = oven_task_of(oven_se);

		if (!task_on_cpu(rq, p) &&
			cpumask_test_cpu(cpu, p->cpus_ptr))
			return p;
	}

	return NULL;
}

/* Add p to the dst_rq */
static void attach_task(struct rq *dst_rq, struct task_struct *p)
{
	struct rq_flags rf;

	rq_lock(dst_rq, &rf);

	update_rq_clock(dst_rq);
	activate_task(dst_rq, p, ENQUEUE_NOCLOCK);
	check_preempt_curr(dst_rq, p, 0);

	rq_unlock(dst_rq, &rf);
}

/* Pull an oven task onto this_rq */
static int pull_oven_task(struct rq *this_rq)
{
	int lowest_cpu_nr_running = 0;
	int lowest_cpu_weight = INT_MAX;
	int lowest_cpu = -1;
	int this_cpu = this_rq->cpu;
	int weight;
	int cpu;

	struct rq *src_rq;

	int ret = 0;

	struct rq_flags rf;

	struct task_struct *p;
	struct oven_rq *oven_rq;

	/* Find the CPU with the highest total weight */
	for_each_online_cpu(cpu) {

		struct rq *rq = cpu_rq(cpu);

		if (cpu == this_cpu)
			continue;

		rq_lock_irqsave(rq, &rf);

		if (rq->oven.oven_nr_running > 1)
		{
			oven_rq = &rq->oven;
			weight = calc_oven_rq_min_weight_not_running(&rq->oven);
			if (weight < lowest_cpu_weight) {
				lowest_cpu_weight = weight;
				lowest_cpu_nr_running = rq->oven.oven_nr_running;
				lowest_cpu = cpu;
			}
		}
		rq_unlock_irqrestore(rq, &rf);
	}

	/* 
     * Return if there is no heaviest cpu, the heaviest cpu has fewer than 2 
	 * tasks running, or if we're the heaviest cpu
     */
	if (lowest_cpu == -1 ||
		lowest_cpu_nr_running < 2 ||
		lowest_cpu == this_cpu)
		return ret;

	/* src_rq is the rq we will pull a task from */
	src_rq = cpu_rq(lowest_cpu);

	rcu_read_lock();

	/* Lock src_rq and find a task to pull */
	rq_lock_irqsave(src_rq, &rf);
	update_rq_clock(src_rq);

	p = pick_pushable_oven_task(src_rq, this_cpu);

	/* Move the task from src_rq to dst_rq */
	if (p) {
		ret = 1;
		deactivate_task(src_rq, p, DEQUEUE_NOCLOCK);
		set_task_cpu(p, this_cpu);
		rq_unlock(src_rq, &rf);

		attach_task(this_rq, p);

		local_irq_restore(rf.flags);
	} else
		rq_unlock_irqrestore(src_rq, &rf);
	rcu_read_unlock();

	return ret;
}

/*
 * idle_balance is called by schedule() if this_cpu is about to become
 * idle. Attempts to pull tasks from other CPUs.
 *
 * Returns:
 *   < 0 - we released the lock and there are !oven tasks present
 *     0 - failed, no new tasks
 *   > 0 - success, new (oven) tasks present
 */
static inline int newidle_balance(struct rq *this_rq, struct rq_flags *rf)
{
	int pulled_task = 0;
	
	rq_unlock(this_rq, rf);
	pulled_task = pull_oven_task(this_rq);
	rq_lock(this_rq, rf);

	update_rq_clock(this_rq);

	/*
	 * While browsing the domains, we released the rq lock, a task could
	 * have been enqueued in the meantime. Since we're not going idle,
	 * pretend we pulled a task.
	 */
	if (this_rq->oven.oven_nr_running && !pulled_task)
		pulled_task = 1;

	/* Is there a task of a high priority class? */
	if (this_rq->nr_running != this_rq->oven.oven_nr_running)
		pulled_task = -1;

	return pulled_task;
}


#else /* !CONFIG_SMP */

static inline int newidle_balance(struct rq *this_rq, struct rq_flags *rf)
{
	return 0;
}

#endif

static void update_curr_oven(struct rq *rq)
{
	struct task_struct *curr = rq->curr;
	u64 delta_exec;
	u64 now;
	
	if (unlikely(curr->sched_class != &oven_sched_class))
		return;

	now = rq_clock_task(rq);
	delta_exec = now - curr->se.exec_start;
	if (unlikely((s64)delta_exec <= 0))
		return;

	schedstat_set(curr->stats.exec_max,
		      max(curr->stats.exec_max, delta_exec));

	curr->se.sum_exec_runtime += delta_exec;
	account_group_exec_runtime(curr, delta_exec);

	curr->se.exec_start = now;
	cgroup_account_cputime(curr, delta_exec);
}

/* Enqueue a task based on its priority */
static void enqueue_task_oven(struct rq *rq, struct task_struct *p, int flags)
{
	struct oven_rq *oven_rq = &rq->oven;
	struct sched_oven_entity *oven_se = &p->oven;
	struct sched_oven_entity *se;

	oven_se->on_rq = 1;

	++oven_rq->oven_nr_running;

	/* Add based on weight */
	list_for_each_entry(se, &oven_rq->oven_rq_list, entity_list) {
		if (oven_se->weight <= se->weight)
		{
			list_add_tail(&oven_se->entity_list, &se->entity_list);
			goto finish;
		}
	}

	list_add_tail(&oven_se->entity_list, &oven_rq->oven_rq_list);

finish:
	add_nr_running(rq, 1);
}

/* Dequeue the given task */
static void dequeue_task_oven(struct rq *rq, struct task_struct *p, int flags)
{
	struct oven_rq *oven_rq = &rq->oven;
	struct sched_oven_entity *oven_se = &p->oven;

	update_curr_oven(rq);

	if (oven_rq->oven_nr_running) {

		oven_se->on_rq = 0;

		list_del_init(&oven_se->entity_list);
		--oven_rq->oven_nr_running;

		sub_nr_running(rq, 1);
	}
}

/* 
 * Requeue the given task, which only needs to change if its weight is
 * OVEN_MAX_WEIGHT
 */
static void
requeue_task_oven(struct rq *rq, struct task_struct *p, int head)
{
	struct sched_oven_entity *oven_se = &p->oven;
	struct oven_rq *oven_rq = oven_rq_of_oven_se(oven_se);

	if (oven_se->on_rq && oven_se->weight == OVEN_MAX_WEIGHT) {
		list_move_tail(&oven_se->entity_list, &oven_rq->oven_rq_list);
	}
}

static void yield_task_oven(struct rq *rq)
{
	requeue_task_oven(rq, rq->curr, 0);
}

/* Check if p has lower weight than rq->curr, in which case p preempts */
static void check_preempt_curr_oven(struct rq *rq, struct task_struct *p,
				   int flags)
{
	struct task_struct *curr = rq->curr;

	if (p->oven.weight < curr->oven.weight) {
		requeue_task_oven(rq, rq->curr, 0);
		resched_curr(rq);
	}
}

struct task_struct *pick_next_task_oven(struct rq *rq,
	struct task_struct *prev, struct rq_flags *rf)
{
	struct sched_oven_entity *oven_se;
	struct oven_rq *oven_rq = &rq->oven;
	struct list_head *head = &oven_rq->oven_rq_list;
	struct task_struct *p;
	int new_tasks;

again:
	if (!sched_oven_runnable(rq)) {
		if (!rf)
			return NULL;

		new_tasks = newidle_balance(rq, rf);

		/*
		 * Because newidle_balance() releases (and re-acquires) rq->lock, it is
		 * possible for any higher priority task to appear. In that case we
		 * must re-start the pick_next_entity() loop.
		 */
		if (new_tasks < 0)
			return RETRY_TASK;

		if (new_tasks > 0)
			goto again;

		return NULL;
	}

	if (prev)
		put_prev_task(rq, prev);

	/* Choose the next task on the run queue */
	oven_se = list_entry(head->next, struct sched_oven_entity, entity_list);
	p = oven_task_of(oven_se);

	p->se.exec_start = rq_clock_task(rq);

	return p;
}

static struct task_struct *__pick_next_task_oven(struct rq *rq)
{
	return pick_next_task_oven(rq, NULL, NULL);
}

static void put_prev_task_oven(struct rq *rq, struct task_struct *p)
{
	// pr_info("put_prev_task_oven\n");
	update_curr_oven(rq);
}

#ifdef CONFIG_SMP

/*
 * Select rq to which p should be added. This should be the rq that allows the
 * given task to run the quickest, meaning it should be the rq with the fewest
 * number of tasks with a weight less than the weight of p
 */
static int select_task_rq_oven(struct task_struct *p, int task_cpu, int flags)
{
	int min_before = INT_MAX;
	int min_cpu = -1;
	int cpu, before;
	struct rq_flags rf;
	
	struct oven_rq *oven_rq;
	struct sched_oven_entity *oven_se = &p->oven;

	for_each_online_cpu(cpu) {
		struct rq *rq = cpu_rq(cpu);

		if (!cpumask_test_cpu(cpu, p->cpus_ptr))
			continue;

		rq_lock(rq, &rf);

		oven_rq = &rq->oven;
		before = calc_oven_rq_num_running_before(oven_rq, oven_se);

		if (before < min_before) {
			min_before = before;
			min_cpu = cpu;
		}

		rq_unlock(rq, &rf);
	}

	return min_cpu;
}

/*
 * Perform balance.
 * Called from put_prev_task_balance() in pick_next_task() in core.c.
 */
static int balance_oven(struct rq *rq, struct task_struct *prev, struct rq_flags *rf)
{
	if (rq->oven.oven_nr_running)
		return 1;

	return newidle_balance(rq, rf) != 0;
}

#endif /* CONFIG_SMP */

static void set_next_task_oven(struct rq *rq, struct task_struct *p, bool first)
{
	p->se.exec_start = rq_clock_task(rq);
}

/*
 * Every kernel tick, task_tick_oven is called.
 */
static void task_tick_oven(struct rq *rq, struct task_struct *p, int queued)
{
	struct oven_rq *oven_rq = &rq->oven;

	update_curr_oven(rq);	

	/* 
	 * If our weight is OVEN_MAX_WEIGHT, and there are other tasks on the run
	 * queue, then reschedule.
	 */
	if (p->oven.weight == OVEN_MAX_WEIGHT &&
		oven_rq->oven_nr_running > 1)
	{
		requeue_task_oven(rq, p, 0);
		resched_curr(rq);
	}
}

/* 
 * We're overriding this function to instead update weights and check
 * if rescheduling should occur.
 */
static void
prio_changed_oven(struct rq *rq, struct task_struct *p, int oldprio)
{
	struct sched_oven_entity *oven_se = &p->oven, *next_se;
	struct oven_rq *oven_rq = &rq->oven;

	/* If we're not on a run queue, don't do any of the checks */
	if (!task_on_rq_queued(p))
		return;

	/* If it's the current task, check if the next task should run instead */
	if (task_current(rq, p)) {
		if (oven_se->entity_list.next != &oven_rq->oven_rq_list) {
			next_se = list_entry(oven_se->entity_list.next,
							struct sched_oven_entity, entity_list);
			if (next_se->weight < oven_se->weight)
			{
				requeue_task_oven(rq, p, 0);
				resched_curr(rq);
			}
		}
	/* Otherwise, try to preempt the current task */
	} else if (oven_task(rq->curr))
		check_preempt_curr_oven(rq, p, 0);
}

/*
 * p just switched to OVEN.
 * Check if p should preempt rq->curr or if we should reschedule
 */
static void switched_to_oven(struct rq *rq, struct task_struct *p)
{
	// pr_info("switched_to_oven\n");
	if (task_on_rq_queued(p)) {
		if (task_current(rq, p))
			resched_curr(rq);
		else
			check_preempt_curr(rq, p, 0);
	}
}

DEFINE_SCHED_CLASS(oven) = {
	.enqueue_task		= enqueue_task_oven,
	.dequeue_task		= dequeue_task_oven,
	.yield_task		= yield_task_oven,
	.check_preempt_curr	= check_preempt_curr_oven,
	.pick_next_task		= __pick_next_task_oven,
	.put_prev_task		= put_prev_task_oven,
#ifdef CONFIG_SMP
	.balance		= balance_oven,
	.select_task_rq		= select_task_rq_oven,
	.set_cpus_allowed       = set_cpus_allowed_common,
#endif
	.set_next_task		= set_next_task_oven,
	.task_tick		= task_tick_oven,
	.prio_changed		= prio_changed_oven,
	.switched_to		= switched_to_oven,
	.update_curr		= update_curr_oven,
};