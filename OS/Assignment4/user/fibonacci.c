#include <stdio.h>
#include <stdlib.h>
#include <sched.h>
#include <errno.h>
#include <unistd.h>

unsigned long long fib(unsigned long long n)
{
	if (n <= 1)
		return n;
	else
		return fib(n - 1) + fib(n - 2);
}

int main(int argc, char** argv)
{
	int n = atoi(argv[1]);
	struct sched_param params = {1000 + n};
	int ret = sched_setscheduler(0, 7, &params);
	if (ret)
	{
		fprintf(stderr, "OVEN scheduling policy does not exist\n");
		exit(1);
	}

	if (argc != 2)
	{
		fprintf(stderr, "Usage: ./fibonacci [number]\n");
		exit(1);
	}

	return fib(n);
}
