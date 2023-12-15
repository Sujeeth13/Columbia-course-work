#include <linux/atomic.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/syscalls.h>
#include <linux/pstrace.h>
#include <linux/rwlock.h>
#include <linux/wait.h>

struct pstrace_kernel pstrace = {
	.head = 0,
	.tail = 0,
	.pstrace_lock = __SPIN_LOCK_UNLOCKED(pstrace_lock),
	.counter = ATOMIC_INIT(0),
	.empty = true,
};

struct pstrace_traced pstrace_traced = {
	.pid = ATOMIC_INIT(INT_MIN),
};

LIST_HEAD(pstrace_evt_list);
DEFINE_SPINLOCK(pstrace_evt_list_lock);

/*
 * Return the task struct of the task given its PID,
 * by iterating over all procs until it is found.
 *
 * An alternative (simpler) solution would just call
 * find_task_by_vpid, but that gets into struct pid's, which
 * add an unnecessary level of confusion.
 */
static struct task_struct *find_task_struct(int pid)
{
	struct task_struct *proc;

	if (pid == 0)
		return &init_task;

	for_each_process(proc) {
		if (task_pid_nr(proc) == pid)
			return proc;
	}

	return NULL;
}


static inline int ring_buffer_idx_inc(int x)
{
	return ++x == PSTRACE_BUF_SIZE ? 0 : x;
}

/* Assume pstrace_lock is held */
static int pstrace_get_buf(struct pstrace_kernel *pst, struct pstrace *buf,
			   int limit)
{
	int head, tail, nr_entries = 0;

	if (pst->empty || limit <= 0)
		return 0;

	head = pst->head;
	tail = pst->tail;

	do {
		memcpy(&buf[nr_entries], &pst->entry[head],
		       sizeof(struct pstrace));
		nr_entries++;
		limit--;
		head = ring_buffer_idx_inc(head);
	} while (head != tail && limit > 0);

	return nr_entries;
}

static bool task_is_pstraced(struct task_struct *p)
{
	bool ret = false;
	s64 pid = atomic_read(&pstrace_traced.pid);

	if (pid == -1 || pid == task_tgid_nr(p))
		ret = true;

	return ret;
}

/* Need pstrace_lock */
static void copy_to_blocked_gets(bool unconditional)
{
	struct pstrace_kernel *pst = &pstrace;
	struct list_head *evt_list, *tmp;
	long pst_counter;

	spin_lock(&pstrace_evt_list_lock);
	list_for_each_safe(evt_list, tmp, &pstrace_evt_list) {
		struct pstrace_evt *evt =
			list_entry(evt_list, struct pstrace_evt, head);

		pst_counter = atomic_read(&pst->counter);
		if (!evt->copied &&
		    (unconditional || evt->counter <= pst_counter)) {
			int limit =
				evt->counter - pst_counter + PSTRACE_BUF_SIZE;

			if (limit <= 0)
				evt->counter = pst_counter;
			evt->nr_entries = pstrace_get_buf(pst, evt->buf, limit);
			evt->copied = true;
		}
	}
	spin_unlock(&pstrace_evt_list_lock);
}

void wake_blocked_gets(void)
{
	struct list_head *evt_list, *tmp;
	bool woken = true;
	unsigned long flags;

	local_irq_save(flags);
	while (woken) {
		woken = false;
		spin_lock(&pstrace_evt_list_lock);
		list_for_each_safe(evt_list, tmp, &pstrace_evt_list) {
			struct pstrace_evt *evt =
				list_entry(evt_list, struct pstrace_evt, head);

			if (evt->copied) {
				evt->woken = true;
				woken = true;
				list_del(&evt->head);
				/* pstrace_add in wake_up_all may need pstrace_evt_list_lock */
				spin_unlock(&pstrace_evt_list_lock);
				wake_up_all(&evt->evt_waitq);
				break;
			}
		}
	}

	spin_unlock(&pstrace_evt_list_lock);
	local_irq_restore(flags);
}

void pstrace_add(struct task_struct *p, long stateoption)
{
	struct pstrace_kernel *pst = &pstrace;
	int tail;
	unsigned long flags;

	if (!p)
		return;

	if (!task_is_pstraced(p))
		return;

	/* add an entry */
	local_irq_save(flags);
	spin_lock(&pst->pstrace_lock);

	tail = pst->tail;
	if (p->exit_state == EXIT_ZOMBIE || p->exit_state == EXIT_DEAD) {
		pst->entry[tail].state = p->exit_state;
	} else if (stateoption != 0) {
		pst->entry[tail].state = stateoption;
	} else {
		pst->entry[tail].state = p->__state;
	}

	strncpy(pst->entry[tail].comm, p->comm, sizeof(p->comm));
	pst->entry[tail].pid = task_tgid_nr(p);
	pst->entry[tail].tid = task_pid_nr(p);

	atomic_add(1, &pst->counter);
	if (unlikely(pst->empty))
		pst->empty = false;
	else if (pst->head == pst->tail)
		pst->head = ring_buffer_idx_inc(pst->head);
	pst->tail = ring_buffer_idx_inc(pst->tail);

	/* copy buffer to evt */
	copy_to_blocked_gets(false);

	spin_unlock(&pst->pstrace_lock);
	local_irq_restore(flags);
}

SYSCALL_DEFINE2(pstrace_get, struct pstrace __user *, buf, long __user *,
		counter)
{
	struct pstrace_evt *evt;
	unsigned long ret = 0;
	unsigned long flags;
	long kcounter, pst_counter;
	struct pstrace_kernel *pst = &pstrace;

	if (!buf || !counter)
		return -EINVAL;

	if (!access_ok(buf, sizeof(struct pstrace) * PSTRACE_BUF_SIZE))
		return -EFAULT;

	if (get_user(kcounter, counter))
		return -EFAULT;

	if (kcounter < 0)
		return -EINVAL;

	evt = kmalloc(sizeof(struct pstrace_evt), GFP_KERNEL);
	if (!evt)
		return -ENOMEM;

	evt->counter = kcounter + PSTRACE_BUF_SIZE;
	evt->woken = false;
	evt->copied = false;
	init_waitqueue_head(&evt->evt_waitq);

	local_irq_save(flags);
	spin_lock(&pst->pstrace_lock);

	pst_counter = atomic_read(&pst->counter);
	if (kcounter == 0 || evt->counter <= pst_counter) {
		int limit = min_t(long, pst_counter, PSTRACE_BUF_SIZE);

		if (kcounter != 0)
			limit = evt->counter - pst_counter + PSTRACE_BUF_SIZE;

		if (kcounter == 0 || limit <= 0)
			evt->counter = pst_counter;

		ret = pstrace_get_buf(pst, evt->buf, limit);

		spin_unlock(&pst->pstrace_lock);
		local_irq_restore(flags);

		goto copy_stuff_to_user;
	}

	spin_unlock(&pst->pstrace_lock);

	spin_lock(&pstrace_evt_list_lock);
	list_add(&evt->head, &pstrace_evt_list);
	spin_unlock(&pstrace_evt_list_lock);
	local_irq_restore(flags);

	wait_event_interruptible(evt->evt_waitq, evt->woken);

	if (!evt->woken) {
		ret = -EINTR;
		local_irq_save(flags);
		spin_lock(&pstrace_evt_list_lock);
		list_del(&evt->head);
		spin_unlock(&pstrace_evt_list_lock);
		local_irq_restore(flags);
		goto out;
	}

	ret = evt->nr_entries;

copy_stuff_to_user:
	if (copy_to_user(buf, evt->buf,
			 PSTRACE_BUF_SIZE * sizeof(struct pstrace)))
		ret = -EFAULT;

	if (put_user(evt->counter, counter))
		ret = -EFAULT;

out:
	kfree(evt);
	return ret;
}

SYSCALL_DEFINE1(pstrace_enable, pid_t, pid)
{
	if (pid != -1 && pid != 0 && !find_task_struct(pid))
		return -ESRCH;

	atomic_set(&pstrace_traced.pid, pid);
	return 0;
}

SYSCALL_DEFINE0(pstrace_disable)
{
	atomic_set(&pstrace_traced.pid, INT_MIN);
	return 0;
}

SYSCALL_DEFINE0(pstrace_clear)
{
	struct pstrace_kernel *pst = &pstrace;
	unsigned long flags;
	long current_pid = atomic_read(&pstrace_traced.pid);

	atomic_set(&pstrace_traced.pid, INT_MIN);
	local_irq_save(flags);
	spin_lock(&pst->pstrace_lock);

	copy_to_blocked_gets(true);
	pst->head = pst->tail;
	pst->empty = true;
	spin_unlock(&pst->pstrace_lock);
	wake_blocked_gets();
	local_irq_restore(flags);
	atomic_set(&pstrace_traced.pid, current_pid);

	return 0;
}