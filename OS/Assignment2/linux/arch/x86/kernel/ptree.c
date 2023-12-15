#include <linux/sched.h>
#include <linux/container_of.h>

#include <linux/processor.h>
#include <asm/stacktrace.h>

unsigned long get_userpc(struct task_struct *task)
{
	struct pt_regs *regs;

	regs = task_pt_regs(task);
	return regs->ip;
}

unsigned long get_kernelpc(struct task_struct *task)
{
	return container_of(task_pt_regs(task), struct fork_frame, regs)
		->frame
		.ret_addr;
}
