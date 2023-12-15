/*
 * W4118:
 * OVEN scheduler
 */

#include <linux/list.h>
#include <linux/sched/cputime.h>
#include "sched.h"


void init_oven_rq(struct oven_rq *oven_rq)
{
	//init spinlock
	INIT_LIST_HEAD(&oven_rq->task_list);
	INIT_LIST_HEAD(&oven_rq->sjf_list);
	oven_rq->oven_nr_running = 0;
}

static inline struct task_struct *oven_get_task_struct(struct sched_oven_entity *oven_se)
{
	return container_of(oven_se, struct task_struct, oven_se);
}

static void requeue_task_oven(struct rq *rq, struct task_struct *curr)
{
	list_move_tail(&curr->oven_se.task_list, &rq->oven_rq.task_list);
}

struct task_struct *pick_next_task_oven(struct rq *rq)
{

	struct task_struct *next;
	struct sched_oven_entity *next_se;
	struct list_head *pos;

	if (!rq->oven_rq.oven_nr_running)
		return NULL;

	if (list_empty(&rq->oven_rq.sjf_list)) {
		next_se = list_first_entry(&rq->oven_rq.task_list,
			struct sched_oven_entity, task_list);
		next = oven_get_task_struct(next_se);
	} else {
		int min_p = 10000;

		list_for_each(pos, &rq->oven_rq.sjf_list) {
			struct task_struct *tsk;
			struct sched_oven_entity *tsk_se;

			tsk_se = list_entry(pos, struct sched_oven_entity, sjf_list);
			tsk = oven_get_task_struct(tsk_se);
			if (tsk_se->weight < min_p) {
				min_p = tsk_se->weight;
				next = tsk;
			}
		}

	}

	if (!next)
		return NULL;

	// printk(KERN_INFO "Task Picked: %s, pid: %d, policy: %d, priority: %d",
	// next->comm, next->pid, next->policy, next->oven_se.weight);

	return next;
}


static void put_prev_task_oven(struct rq *rq, struct task_struct *p)
{
}

static void set_next_task_oven(struct rq *rq, struct task_struct *next, bool first)
{
}

/*
 * Preempt the current task with a newly woken task if needed:
 */
static void check_preempt_curr_oven(struct rq *rq, struct task_struct *p, int flags)
{

	// useful for shortest job first
	if (p->oven_se.weight < current->oven_se.weight)
		resched_curr(rq);
}

static void enqueue_task_oven(struct rq *rq, struct task_struct *p, int flags)
{
	struct sched_oven_entity *oven_se = &p->oven_se;
	struct task_struct *curr_task = oven_get_task_struct(oven_se);
	struct list_head *runq;

	if (oven_se->weight == 10000) {
		runq = &rq->oven_rq.task_list;
		if (list_empty(runq))
			list_add(&oven_se->task_list, runq);
		else
			list_add_tail(&oven_se->task_list, runq);
		//printk(KERN_INFO "ENQUEUE RR");
	} else {
		runq = &rq->oven_rq.sjf_list;

		if (list_empty(runq))
			list_add(&oven_se->sjf_list, runq);
		else
			list_add_tail(&oven_se->sjf_list, runq);
		//printk(KERN_INFO "ENQUEUE SJF");

	}

	oven_se->on_rq = 1;
	++rq->oven_rq.oven_nr_running;

	add_nr_running(rq, 1);

	//printk(KERN_INFO "Enqueued Task: %s, pid: %d, policy: %d, prio: %d", curr_task->comm,
	// curr_task->pid, curr_task->policy, oven_se->weight);
}

static void dequeue_task_oven(struct rq *rq, struct task_struct *p, int flags)
{
	struct sched_oven_entity *oven_se = &p->oven_se;
	struct task_struct *curr_task = oven_get_task_struct(oven_se);
	//printk(KERN_INFO "Dequeued Task: %s, pid: %d, policy: %d, prio: %d",
	//curr_task->comm, curr_task->pid, curr_task->policy, oven_se->weight);

	//if (p->prio == 10000) {
	if (oven_se->weight == 10000) {
		list_del_init(&oven_se->task_list);
		//printk(KERN_INFO "DEQUEUE RR");

	} else {
		list_del_init(&oven_se->sjf_list);
		//printk(KERN_INFO "DEQUEUE SJF");
	}
	oven_se->on_rq = 0;

	--rq->oven_rq.oven_nr_running;
	sub_nr_running(rq, 1);

}

static void yield_task_oven(struct rq *rq)
{
}

static int can_migrate_oven(struct task_struct *p, struct rq *rq, struct rq *grab_rq)
{
	if (!oven_policy(p->policy))
		return 0;
	if (p->pid == grab_rq->curr->pid) // checks if TASK_RUNNING (not runnable)
		return 0;
	if (!cpu_active(rq->cpu))
		return 0;
	if (kthread_is_per_cpu(p))
		return 0;
	if (task_cpu(p) != grab_rq->cpu)
		return 0;

	return 1;
}

static struct task_struct *grab_task_oven(struct rq *rq, struct rq *grab_rq)
{
	struct task_struct *tsk;
	struct sched_oven_entity *tsk_se;
	struct list_head *pos;

	if (!list_empty(&grab_rq->oven_rq.sjf_list)) {
		// handling SJF TASKS

		int max_p = -1;

		list_for_each(pos, &grab_rq->oven_rq.sjf_list) {

			struct task_struct *p;
			struct sched_oven_entity *p_se;

			p_se = list_entry(pos, struct sched_oven_entity, sjf_list);
			p = oven_get_task_struct(p_se);

			if (p_se->weight > max_p) {
				if (can_migrate_oven(p, rq, grab_rq)) {
					max_p = p_se->weight;
					tsk = p;
				}
			}
		}
		if (max_p != -1)
			return tsk;
	} else {

		// HANDLES RR TASKS
		if (list_empty(&grab_rq->oven_rq.task_list))
			return NULL;

		list_for_each(pos, &grab_rq->oven_rq.task_list) {
			tsk_se = list_entry(pos, struct sched_oven_entity, task_list);
			tsk = oven_get_task_struct(tsk_se);
			if (can_migrate_oven(tsk, rq, grab_rq))
				return tsk;
		}
	}
	return NULL;
}

static int balance_oven(struct rq *rq, struct task_struct *p, struct rq_flags *rf)
{
	int curr_cpu, cpu;
	struct rq *grab_rq;
	struct task_struct *tsk;
	int max_nr_running = -1;

	curr_cpu = rq->cpu;

	rq_unpin_lock(rq, rf);
	if (rq->nr_running != 0) {
		rq_repin_lock(rq, rf);
		//printk(KERN_INFO "No balancing done since rq is not empty");
		return 0;
	}

	for_each_online_cpu(cpu) {
		struct rq *temp;

		if (curr_cpu == cpu)
			continue;

		grab_rq = cpu_rq(cpu);

		// migration of the task
		if (grab_rq->nr_running < 2)
			continue;
		double_lock_balance(rq, grab_rq);
		tsk = grab_task_oven(rq, grab_rq);

		// if no task in run queue that can be migrated
		if (!tsk) {
			double_unlock_balance(rq, grab_rq);
			rq_repin_lock(rq, rf);
			//printk(KERN_INFO "No balancing done since no task found");
			continue;
		}
		dequeue_task_oven(grab_rq, tsk, 0);
		set_task_cpu(tsk, curr_cpu);
		enqueue_task_oven(rq, tsk, 0);
		double_unlock_balance(rq, grab_rq);
		resched_curr(rq);
		rq_repin_lock(rq, rf);
		// printk(KERN_INFO "Process %d (name: %s) moved from CPU: %d to CPU: %d",
		//	tsk->pid, tsk->comm, grab_rq->cpu, rq->cpu);
		return 1;
	}

	return 0;
}

static struct task_struct *pick_task_oven(struct rq *rq)
{
	//return pick_next_task_oven(rq);
	return NULL;
}

static int select_task_rq_oven(struct task_struct *p, int cpu, int flags)
{
	return 0;
}


static void task_tick_oven(struct rq *rq, struct task_struct *curr, int queued)
{
	struct sched_oven_entity *oven_se = &curr->oven_se;

	if (oven_se->weight != 10000)
		return;

	if (oven_se->task_list.prev != oven_se->task_list.next) {

	//if (oven_se->task_list.prev != oven_se->task_list.next) {
		requeue_task_oven(rq, curr);
		resched_curr(rq);
		return;
	}
}

// if a task's scheduling policy is switched to oven
static void switched_to_oven(struct rq *rq, struct task_struct *p)
{
}

// if priority changes. Initial RR does not need to handle this
static void prio_changed_oven(struct rq *rq, struct task_struct *p, int oldprio)
{
}

static void update_curr_oven(struct rq *rq)
{
}


DEFINE_SCHED_CLASS(oven) = {
	.enqueue_task		= enqueue_task_oven,
	.dequeue_task		= dequeue_task_oven,
	.yield_task			= yield_task_oven,

	.check_preempt_curr	= check_preempt_curr_oven,

	.pick_next_task		= pick_next_task_oven,
	.put_prev_task		= put_prev_task_oven,
	.set_next_task      = set_next_task_oven,

#ifdef CONFIG_SMP
	.balance			= balance_oven,
	.pick_task			= pick_task_oven,
	.select_task_rq		= select_task_rq_oven,
	.set_cpus_allowed	= set_cpus_allowed_common,
#endif

	.task_tick			= task_tick_oven,

	.prio_changed		= prio_changed_oven,
	.switched_to		= switched_to_oven,
	.update_curr		= update_curr_oven,
};
