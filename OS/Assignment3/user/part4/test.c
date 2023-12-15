#define _GNU_SOURCE
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <wait.h>
#include <time.h>
#include <pthread.h>
#include <stdbool.h>
#include <libgen.h>
#include <assert.h>
#include <fcntl.h>
#include <linux/pstrace.h>

#define TASK_RUNNING 0x0000
#define TASK_INTERRUPTIBLE 0x0001
#define TASK_UNINTERRUPTIBLE 0x0002
#define TASK_RUNNABLE 0x0003
#define __TASK_STOPPED 0x0004
#define EXIT_DEAD 0x0010
#define EXIT_ZOMBIE 0x0020
#define TASK_WAKEKILL 0x0100
#define TASK_STOPPED (TASK_WAKEKILL | __TASK_STOPPED)

#define BUF_SIZE 500

#define __NR_SYSCALL_PSTRACE_ENABLE 451
#define __NR_SYSCALL_PSTRACE_DISABLE 452
#define __NR_SYSCALL_PSTRACE_GET 453
#define __NR_SYSCALL_PSTRACE_CLEAR 454

struct pstrace_entry {
	char comm[16];
	long state;
	pid_t pid;
	pid_t tid;
};

int childExited;

void *monitorChild(void *arg)
{
	pid_t pid = *(pid_t *)arg;

	waitpid(pid, NULL, 0);

	childExited = true;
	syscall(__NR_SYSCALL_PSTRACE_CLEAR);

	return NULL;
}

int main(int argc, char **argv)
{
	struct pstrace_entry buf[BUF_SIZE];
	int ret = 0, j = 0, nr_entries = 0;
	long counter = 0;
	pid_t pid;
	long state, state_trim;
	bool ran = false, slept = false, slept_a_lot = false, runnable = false, stopped = false;
	int dead = 0, zombie = 0;

	pid = fork();
	if (!pid) {
		sleep(1);
		int x = execl("./seven_states", "./seven_states", NULL);
		fprintf(stderr, "%d, %d\n", x, errno);
		if (x < 0)
			return -1;
	} else if (pid < 0) {
		fprintf(stderr, "fork failed\n");
		return -1;
	}

	pthread_t monitorThread;
	pthread_create(&monitorThread, NULL, monitorChild, &pid);

	syscall(__NR_SYSCALL_PSTRACE_DISABLE);
	syscall(__NR_SYSCALL_PSTRACE_CLEAR);

	fprintf(stderr, "pstrace_enable: %d\n", pid);
	ret = syscall(__NR_SYSCALL_PSTRACE_ENABLE, pid);
	if (ret)
		fprintf(stderr, "err: %s\n", strerror(errno));

	kill(pid, SIGSTOP);
	fprintf(stderr, "stopped: %d\n", pid);
	sleep(2);
	kill(pid, SIGCONT);

	while (!childExited) {
		fprintf(stderr, "pstrace_get: %d\n", pid);
		nr_entries = syscall(__NR_SYSCALL_PSTRACE_GET, buf, &counter);
		if (nr_entries < 0)
			fprintf(stderr, "err: %s\n", strerror(errno));

		for (j = 0; j < nr_entries; j++) {
			fprintf(stderr, "pid=%d tid=%d comm=%s state=%ld\n", buf[j].pid,
						buf[j].tid, buf[j].comm, buf[j].state);

			state = buf[j].state;
			state_trim = state & 0xff;

			ran = ran || state_trim == TASK_RUNNING;
			slept = slept || state_trim == TASK_INTERRUPTIBLE;
			runnable = runnable || state_trim == TASK_RUNNABLE;
			dead += state_trim == EXIT_DEAD;
			zombie += state_trim == EXIT_ZOMBIE;
			slept_a_lot = slept_a_lot || state_trim == TASK_UNINTERRUPTIBLE;
			stopped = stopped || state_trim & __TASK_STOPPED;
		}
	}

	syscall(__NR_SYSCALL_PSTRACE_DISABLE);
	syscall(__NR_SYSCALL_PSTRACE_CLEAR);

	pthread_join(monitorThread, NULL);

	if (!ran)
		fprintf(stderr, "INFO: never detected TASK_RUNNING (pid = %d)\n", pid);
	if (!runnable)
		fprintf(stderr, "INFO: never detected TASK_RUNNABLE (pid = %d)\n", pid);
	if (!slept)
		fprintf(stderr, "INFO: never detected TASK_INTERRUPTIBLE (pid = %d)\n", pid);
	if (!dead)
		fprintf(stderr, "INFO: never detected EXIT_DEAD (pid = %d)\n", pid);
	if (!zombie)
		fprintf(stderr, "INFO: never detected EXIT_ZOMBIE (pid = %d)\n", pid);
	if (!slept_a_lot)
		fprintf(stderr, "INFO: never detected TASK_UNINTERRUPTIBLE (pid = %d)\n", pid);
	if (!stopped)
		fprintf(stderr, "INFO: never detected TASK_STOPPED (pid = %d)\n", pid);

	return 0;
}
