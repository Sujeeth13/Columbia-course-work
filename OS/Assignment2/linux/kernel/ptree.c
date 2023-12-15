#include <linux/kernel.h>
#include <linux/syscalls.h>
#include <linux/printk.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/string.h>
#include <linux/pid.h>
#include <linux/rcupdate.h>
#include <linux/list.h>
#include <linux/list_sort.h>
#include <linux/uaccess.h>

unsigned long get_userpc(struct task_struct *task);
unsigned long get_kernelpc(struct task_struct *task);

struct data_struct {
	int level;
	struct task_struct *t;
	struct list_head list;
};

/* This pushes data point to beginning of list for stack implementation*/
void push(struct list_head *head, struct data_struct *data)
{
	list_add(&data->list, head);
}

/* This returns the data point in the beginning of the list*/
struct data_struct *pop(struct list_head *head)
{
	struct data_struct *item;

	if (list_empty(head))
		return NULL;
	item = list_first_entry(head, struct data_struct, list);
	list_del(&item->list);
	return item;
}

/* This pushes data point to end of list for queue implementation*/
void enqueue(struct list_head *head, struct data_struct *data)
{
	list_add_tail(&data->list, head);
}

void copy_task(struct tskinfo *to, struct data_struct *from)
{
	to->pid = from->t->pid;
	to->tgid = from->t->tgid;
	to->parent_pid = from->t->real_parent->pid;
	strlcpy(to->comm, from->t->comm, sizeof(to->comm));
	to->level = from->level;
	to->userpc = get_userpc(from->t);
	to->kernelpc = get_kernelpc(from->t);

}

static int cmp_func(void *priv,
		const struct list_head *a, const struct list_head *b)
{
	struct data_struct *task_a = list_entry(a, struct data_struct, list);
	struct data_struct *task_b = list_entry(b, struct data_struct, list);

	if (task_a->t->pid < task_b->t->pid)
		return -1;
	if (task_a->t->pid > task_b->t->pid)
		return 1;
	return 0;
}

int ptree(struct tskinfo *buf, int *nr, int root_id)
{
	struct task_struct *task;
	int knr;
	const int BUFF_SIZE = 1024;
	int buffsize;
	struct tskinfo *kbuf;
	struct data_struct *mem;
	struct list_head queue;
	struct list_head threads;
	struct data_struct *first_data;
	struct task_struct *thread;
	int num_process;
	int mem_ctr;
	unsigned long bytes_not_copied;

	get_user(knr, nr);
	if (nr == NULL || buf == NULL)
		return -EINVAL;
	if (knr < 0)
		return -EINVAL;
	if (!access_ok(buf, sizeof(struct tskinfo)*BUFF_SIZE)
			|| !access_ok(nr, sizeof(int)))
		return -EFAULT;
	buffsize = knr;
	if (buffsize == 0) {
		buffsize = BUFF_SIZE;
		knr = BUFF_SIZE;
	}
	kbuf = kmalloc(sizeof(struct tskinfo) * buffsize, GFP_KERNEL);

	mem = kmalloc(sizeof(struct data_struct) * knr, GFP_KERNEL);

	if (kbuf == NULL || mem == NULL) {
		printk(KERN_ERR "Not enough memory\n");
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&queue);

	INIT_LIST_HEAD(&threads);

	mem_ctr = 0;
	first_data = &mem[mem_ctr++];

	rcu_read_lock();
	for_each_process(task) { /*add error check if root_id doesn't exist*/
		if (task->pid == root_id)
			break;
	}
	if (root_id != 0 && task->pid == 0) /*If inputed task pid doesn't exit*/
		return -EINVAL;
	first_data->t = task;
	first_data->level = 0;

	num_process = 0;
	enqueue(&queue, first_data);

	for_each_thread(task, thread) {
		struct data_struct *first_data_thread;

		if (task->pid == thread->pid)
			continue;
		if (mem_ctr == knr)
			break;
		first_data_thread = &mem[mem_ctr++];
		first_data_thread->t = thread;
		first_data_thread->level = 0;
		enqueue(&threads, first_data_thread);
	}
	if (!list_empty(&threads)) {
		struct list_head *t_pos, *safe;

		list_sort(NULL, &threads, cmp_func);
		list_for_each_safe(t_pos, safe, &threads) {
			struct data_struct *temp;

			temp = list_entry(t_pos, struct data_struct, list);
			list_del(&temp->list);
			enqueue(&queue, temp);
		}
	}

	while (!list_empty(&queue)) {
		struct data_struct *item = pop(&queue);
		struct tskinfo inp;
		struct list_head *pos;

		if (item->t == NULL)
			continue;
		num_process++;
		if (num_process > knr)
			break;

		copy_task(&inp, item);
		memcpy(kbuf+(num_process-1), &inp, sizeof(struct tskinfo));

		printk(KERN_DEFAULT
			"PID: %d, TGID: %d, PPID: %d, Name: %s, Level: %d\n",
			inp.pid, inp.tgid, inp.parent_pid, inp.comm, inp.level);

		list_for_each(pos, &item->t->children) {
			struct data_struct *temp;
			struct task_struct *thread;
			struct task_struct *child;
			struct list_head temp_threads;

			if (mem_ctr  == knr)
				break;
			temp = &mem[mem_ctr++];
			child = list_entry(pos, struct task_struct, sibling);
			temp->t = child;
			temp->level = inp.level + 1;
			enqueue(&queue, temp);

			INIT_LIST_HEAD(&temp_threads);
			for_each_thread(child, thread) {
				struct data_struct *thread_temp;

				if (mem_ctr > knr)
					break;
				if (child->pid == thread->pid)
					continue;
				thread_temp = &mem[mem_ctr++];
				thread_temp->t = thread;
				thread_temp->level = inp.level + 1;
				enqueue(&temp_threads, thread_temp);
			}
			if (!list_empty(&temp_threads)) {
				struct list_head *t_pos, *safe;

				list_sort(NULL, &temp_threads, cmp_func);
				list_for_each_safe(t_pos, safe, &temp_threads) {
					struct data_struct *temp2;

					temp2 = list_entry(t_pos, struct data_struct, list);

					list_del(&temp2->list);
					enqueue(&queue, temp2);
				}
			}
		}
	}
	rcu_read_unlock();
	bytes_not_copied = copy_to_user(buf, kbuf, num_process * sizeof(struct tskinfo));
	if (bytes_not_copied > 0)
		printk(KERN_ERR "%lu bytes not copied", bytes_not_copied);

	kfree(kbuf);
	kfree(mem);
	return 0;
}

SYSCALL_DEFINE3(ptree, struct tskinfo*, buf, int*, nr, int, root_id)
{
	return ptree(buf, nr, root_id);
}
