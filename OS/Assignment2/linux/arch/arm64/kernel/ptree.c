#include <linux/sched.h>
#include <linux/sched/task_stack.h>

#include <linux/processor.h>

unsigned long get_kernelpc(struct task_struct *task)
{
	return task->thread.cpu_context.pc;
}
