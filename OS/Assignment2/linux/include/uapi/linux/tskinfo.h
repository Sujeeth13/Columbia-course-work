#ifndef _UAPI_LINUX_TSKINFO_H_
#define _UAPI_LINUX_TSKINFO_H_

#include <linux/types.h>
#include <linux/unistd.h>

struct tskinfo {
	pid_t parent_pid;       /* process id of parent */
	pid_t pid;              /* process id */
	pid_t tgid;             /* thread group id */
	char comm[16];          /* name of program executed */
	int level;              /* level of this process in the subtree */
	unsigned long userpc;	/* program counter for returning from kernel mode to user mode */
	unsigned long kernelpc;	/* program counter for returning from context switch */

};

#endif /* _UAPI_LINUX_TSKINFO_H_ */
