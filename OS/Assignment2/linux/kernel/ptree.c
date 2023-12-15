#include <linux/tskinfo.h>
#include <linux/syscalls.h>
#include <linux/compiler_types.h>
#include <linux/cred.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/sched/task.h>
#include <linux/kfifo.h>

struct tskinfo;

extern unsigned long get_kernelpc(struct task_struct *task);

/*
 * Return the task struct of the root task given its PID,
 * by iterating over all procs and threads until it is found.
 *
 * An alternative (simpler) solution would just call
 * find_task_by_vpid, but that gets into struct pid's, which
 * add an unnecessary level of confusion.
 */
static struct task_struct *get_root(int root_pid)
{
	struct task_struct *proc, *thread;

	if (root_pid == 0)
		return &init_task;

	/*
	 * We search through each thread, and if root_pid matched a thread we
	 * return the task_struct of the main thread, not the thread with pid
	 * root_pid, so that the first thread group we save is guaranteed to be
	 * sorted in ascending order of PID (ignoring rollover).
	 */
	for_each_process_thread(proc, thread) {
		if (task_pid_nr(thread) == root_pid)
			return proc;
	}

	return NULL;
}

/*
 * Save info about a single task to the given task_info struct pointer.
 */
static inline void save_task(struct task_struct *task, struct tskinfo *task_info, int level)
{
	get_task_comm(task_info->comm, task);
	task_info->pid = task_pid_nr(task);
	task_info->tgid = task_tgid_nr(task);
	task_info->parent_pid = task_pid_nr(task->real_parent);
	task_info->level = level;
	task_info->userpc = instruction_pointer(task_pt_regs(task));
	task_info->kernelpc = get_kernelpc(task);
}

/*
 * Save info from at most nr tasks from the thread group of main_task into buf. Returns the number
 * of task info's actually added to buf.
 */
static int save_thread_group(struct task_struct *main_task, struct tskinfo *buf, int nr, int level)
{
	struct task_struct *t;
	int count;

	if (!main_task)
		return 0;

	t = main_task;
	count = 0;
	do {
		if (count >= nr)
			break;

		save_task(t, &buf[count], level);
		count++;
	} while_each_thread(main_task, t);

	return count;

}

/*
 * Perform BFS over process tree using standard queue-based algorithm.
 * Returns -errno or the number of tasks stored in buf.
 *
 * Implementation note: Counters are slightly complicated by the fact that
 * even though we are iterating (mostly) over processes, we really want to track
 * all tasks (including threads).
 */
static int do_ptree(struct tskinfo *buf, int nr, int root_pid)
{
	struct task_struct *level_root_proc, *curr_proc, *curr_thread, *child_proc;
	int total_tasks_saved, total_tasks_enqueued, tmp_thread_count;
	int temp_ret, ret, curr_level, new_level_flag;
	struct kfifo proc_queue;

	/* Allocate queue memory and initialize counters */
	temp_ret = kfifo_alloc(&proc_queue, nr * sizeof(struct task_struct *), GFP_KERNEL);
	if (temp_ret)
		return temp_ret;

	curr_level = -1;
	total_tasks_enqueued = 0;
	total_tasks_saved = 0;

	rcu_read_lock(); /* Enter RCU critical section, no sleeping */

	/* Get and enqueue root task_struct */
	level_root_proc = get_root(root_pid);
	if (!level_root_proc) {
		ret = -ESRCH;
		goto EXIT;
	}

	temp_ret = kfifo_in(&proc_queue, &level_root_proc, sizeof(level_root_proc));
	if (temp_ret != sizeof(struct task_struct *)) {
		WARN_ON(true); /* This almost certainly can't happen */
		ret = -ENOMEM;
		goto EXIT;
	}

	/*
	 * Enqueuing a proc really means enqueuing all of the threads in proc's
	 * thread group, update counters accordingly.
	 */
	tmp_thread_count = get_nr_threads(level_root_proc);
	total_tasks_enqueued = min(nr, total_tasks_enqueued + tmp_thread_count);

	/*
	 * Main BFS loop: Until queue empty, 'process' every task in FIFO order.
	 * Process by saving to output buffer, then enqueuing all children (unless
	 * total_tasks_enqueued == nr; then we keep processing but stop enqueuing).
	 */
	while (!kfifo_is_empty(&proc_queue)) {
LOOP:
		/* Dequeue next process */
		temp_ret = kfifo_out(&proc_queue, &curr_proc, sizeof(curr_proc));
		if (temp_ret != sizeof(struct task_struct *)) {
			WARN_ON(true); /* This almost certainly can't happen */
			ret = -ENOMEM;
			goto EXIT;
		}

		/*
		 * If we just dequeued the 'level root' node, we are at a new level,
		 * so increment level counter and raise flag to indicate the upcoming
		 * level root should be marked when children are enqueued. Otherwise,
		 * leave the flag/count be because we still need to set a new root.
		 */
		if (level_root_proc == curr_proc) {
			new_level_flag = 1;
			curr_level++;
		}


		/* Save all tasks in curr_proc's thread group up until total_tasks_saved = nr */
		tmp_thread_count = save_thread_group(curr_proc, buf,
							nr - total_tasks_saved, curr_level);

		total_tasks_saved += tmp_thread_count;
		buf += tmp_thread_count;

		/* We should never end up with more tasks saved than requested */
		WARN_ON(total_tasks_saved > nr);

		/* If we have saved nr tasks, we are finished, exit loop */
		if (total_tasks_saved == nr) {
			WARN_ON(total_tasks_enqueued != total_tasks_saved);
			WARN_ON(!kfifo_is_empty(&proc_queue));
			break;
		}

		/*
		 * Iterate over children from ANY task in curr_proc's thread group and
		 * enqueue each unless we have already enqueued nr tasks.
		 */
		if (total_tasks_enqueued == nr)
			continue;
		/*
		 * Requring a loop over both threads AND children is hard to spot, but
		 * necessary to catch children created by fork from within a pthread.
		 */
		for_each_thread(curr_proc, curr_thread) {
			list_for_each_entry_rcu(child_proc, &curr_thread->children, sibling) {
				/* If we're on a new level, save first child as level root */
				if (new_level_flag)
					level_root_proc = child_proc;

				new_level_flag = 0;

				/* Enqueue child */
				temp_ret = kfifo_in(&proc_queue, &child_proc, sizeof(child_proc));
				if (temp_ret != sizeof(struct task_struct *)) {
					WARN_ON(true);
					ret = -ENOMEM;
					goto EXIT;
				}

				tmp_thread_count = get_nr_threads(child_proc);
				if (tmp_thread_count + total_tasks_enqueued >= nr) {
					total_tasks_enqueued = nr;
					/* If we've enqueued nr tasks, break child iteration loop */
					goto LOOP;
				}

				/* Add number of threads associated with proc to count */
				total_tasks_enqueued += tmp_thread_count;
			}
		}

	}

	ret = total_tasks_saved;

EXIT:
	rcu_read_unlock(); /* Exit RCU critical section */
	kfifo_free(&proc_queue);
	return ret;
}


long ptree(struct tskinfo __user *buf, int __user *nr, int root_pid)
{
	int knr, size, total;
	struct tskinfo *kbuf;

	/* Validate inputs */
	if (!buf || !nr)
		return -EINVAL;
	if (get_user(knr, nr)) /* Copy simple variable from userspace to kernelspace */
		return -EFAULT;
	if (knr < 1)
		return -EINVAL;

	/* Allocate kernel buffer */
	size = knr * sizeof(struct tskinfo);
	kbuf = kmalloc(size, GFP_KERNEL);
	/* Could add 'alloc_slow' to allow allocating near limits of memory */
	if (!kbuf)
		return -ENOMEM;

	total = do_ptree(kbuf, knr, root_pid);
	if (total < 0) {
		kfree(kbuf);
		return total;
	}

	/* Should never return more than requested */
	WARN_ON(total > knr);

	/* Adjust size/knr based on how many tasks actually found */
	knr = total;
	size = knr * sizeof(struct tskinfo);

	/* Copy knr and kbuf to userspace */
	if (put_user(knr, nr) || copy_to_user(buf, kbuf, size)) {
		kfree(kbuf);
		return -EFAULT;
	}

	kfree(kbuf);
	return 0;
}

SYSCALL_DEFINE3(ptree, struct tskinfo __user *, buf, int __user *, nr, int, root_pid)
{
	return ptree(buf, nr, root_pid);
}
