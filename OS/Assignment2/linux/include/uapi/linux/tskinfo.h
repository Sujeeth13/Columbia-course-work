#include <linux/kernel.h>
#include <linux/types.h>

struct tskinfo {
	int pid;
	int tgid;
	int parent_pid;
	int level;
	char comm[16];
	unsigned long userpc;
	unsigned long kernelpc;
};

