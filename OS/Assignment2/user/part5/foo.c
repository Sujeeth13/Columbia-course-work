#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>

int my_fork(void)
{
	int x;

	x = fork();
	if (x < 0) {
		fprintf(stderr, "error: %s.", strerror(errno));
		exit(1);
	}
	return x;
}

void *start_thread(void *arg)
{
	printf("starting thread with thid %d\n", gettid());
	while (1)
		;
}

int main(void)
{
	int x, y;
	pthread_t thid;

	while (1) {
		x = my_fork();
		if (x)
			exit(0);
		else {
			x = getpid();
			if (x == 5000) {
				pthread_create(&thid, NULL, start_thread, NULL);
				break;
			}
		}
	}

	y = my_fork();			/* 5002 created, ppid=5000 */

	if (y)
		my_fork();			/* 5003 created, ppid=5000 */

	if (getpid() == 5002) {
		while (kill(5003, SIGWINCH))
			;				/* wait for 5003 to exist */
		my_fork();			/* 5004 created, ppid=5002 */
	}

	else if (getpid() == 5003) {
		while (kill(5004, SIGWINCH))
			;				/* wait for 5004 to exist */
		my_fork();			/* 5005 created, ppid=5003 */
	}

	printf("getpid is %d, getppid is %d\n", getpid(), getppid());

	while (1)
		;

	return 0;
}
