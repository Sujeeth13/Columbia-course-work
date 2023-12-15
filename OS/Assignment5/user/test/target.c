#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <fcntl.h>
#include <sys/wait.h>

#include <linux/shadowpt.h>

int rec(int a)
{
	if (a == 0 || a == 1)
		return 1;
	return rec(a-1) + rec(a-2);
}

int main(void)
{
	int *arr;
	int a;
	pid_t pid;

	printf("PID: %d\n", getpid());

	scanf("%d", &a);

	arr = mmap((void *)281473402814464, 1024*sizeof(int), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	scanf("%d", &a);

	arr[0] = 1000;

	if (arr == MAP_FAILED) {
		perror("mmap");
	}

	scanf("%d", &a);

	mprotect(arr, 1024 * sizeof(int), PROT_READ);

	scanf("%d", &a);

	mprotect(arr, 1024 * sizeof(int), PROT_WRITE);
	arr[0] = 1;

	scanf("%d", &a);

	pid = fork();
	if (pid == 0) {
		char *args[] = { "./do_nothing", NULL };
		if (execve("./do_nothing", args, NULL) == -1) {
			perror("execve");
			return 1;
		}
	}
	scanf("%d", &a);

	arr[0] = 1;

	scanf("%d", &a);

	if (munmap(arr, 1024 * sizeof(int)) == -1) {
		perror("munmap");
	}
	wait(NULL);
	scanf("%d", &a);
	return 0;
}
