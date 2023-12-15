#include <stdio.h>
#include <stdlib.h>
#include <sched.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

unsigned long long fib(unsigned long long n)
{
	if (n <= 1)
		return n;
	else
		return fib(n - 1) + fib(n - 2);
}

int main(int argc, char **argv)
{

	int base;

	base = 200;

	printf("PROC ID: %d\n", getpid());
	if (argc != 2) {
		fprintf(stderr, "Usage: ./fibonacci [number] [prio]\n");
		exit(1);
	}
	unsigned long long n;

	n = atoi(argv[1]);

	struct sched_param params = {.sched_priority = base + n};

	int ret;

	ret = sched_setscheduler(0, 7, &params);

	if (ret) {
		printf("ERROR: %d\n", ret);
		fprintf(stderr, "OVEN scheduling policy does not exist, %s\n", strerror(errno));
		exit(1);
	}

	return fib(n);
}
