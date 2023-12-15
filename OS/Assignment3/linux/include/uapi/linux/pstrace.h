#ifndef _UAPI_LINUX_PSTRACE_H_
#define _UAPI_LINUX_PSTRACE_H_

#define TASK_RUNNABLE			0x0003

struct pstrace {
	char comm[16];
	long state;
	pid_t pid;
	pid_t tid;
};

#endif /* _UAPI_LINUX_PSTRACE_H_ */