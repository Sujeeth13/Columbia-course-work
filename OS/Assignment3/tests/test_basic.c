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

#define TEST(cond) \
	printf("%s: " #cond "\n", (cond) ? "PASS" : "FAIL")

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
#define THREAD_NUM 5

#define __NR_SYSCALL_PSTRACE_ENABLE 451
#define __NR_SYSCALL_PSTRACE_DISABLE 452
#define __NR_SYSCALL_PSTRACE_GET 453
#define __NR_SYSCALL_PSTRACE_CLEAR 454

struct pstrace_entry
{
	char comm[16];
	long state;
	pid_t pid;
	pid_t tid;
};

void *target(void *arg)
{
	struct timespec time = {.tv_sec = 0, .tv_nsec = 250000000L}, time2;
	for (int i = 0; i < 5; i++) // extracts sleeping
		nanosleep(&time, &time2);
	pthread_exit(NULL);
}

int do_wait_and_loop(void)
{
	int i;
	pthread_t threads[THREAD_NUM];

	for (i = 0; i < THREAD_NUM; i++)
		pthread_create(&threads[i], NULL, &target, NULL);

	for (i = 0; i < THREAD_NUM; i++)
		pthread_join(threads[i], NULL);

	return 0;
}

void random_path(char *path, size_t length)
{
	int num;

	for (size_t i = 0; i < length - 1; i++)
	{
		path[i] = 'a' + rand() % 26;
	}

	path[length - 1] = '\0';
}

int main(int argc, char **argv)
{
	struct pstrace_entry buf[BUF_SIZE];
	int ret = 0, j = 0, nr_entries = 0;
	long counter = 0;
	pid_t pid, pid2;
	int status;

	pid = fork();
	if (!pid)
	{
		sleep(1);
		do_wait_and_loop();

		srand(time(NULL));

		for (int i = 0; i < 5; i++)
		{
			char path[16];
			random_path(path, sizeof(path));

			// clear cache
			pid2 = fork();
			if (!pid2)
			{
				fprintf(stderr, "Clearing cache... ");
				int x = execl("./clear_cache.sh", "./clear_cache.sh", NULL);
				fprintf(stderr, "%d, %d\n", x, errno);
				if (x < 0)
					return -1;
			}
			else if (pid2 < 0)
			{
				fprintf(stderr, "fork failed\n");
				return -1;
			}

			waitpid(pid2, NULL, 0);

			int fd = open(path, O_RDONLY);
			if (fd == -1)
			{
				perror(path);
			}
			else
			{
				fprintf(stderr, "Successfully opened %s\n", path);
				close(fd);
			}

			sleep(1);
		}

		return 0;
	}
	else if (pid < 0)
	{
		fprintf(stderr, "fork err: %s", strerror(errno));
		return -1;
	}

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

	waitpid(pid, NULL, 0);

	fprintf(stderr, "pstrace_get: %d\n", pid);
	nr_entries = syscall(__NR_SYSCALL_PSTRACE_GET, buf, &counter);
	if (nr_entries < 0)
		fprintf(stderr, "err: %s\n", strerror(errno));

	syscall(__NR_SYSCALL_PSTRACE_DISABLE);
	syscall(__NR_SYSCALL_PSTRACE_CLEAR);

	fprintf(stderr, "nr_entries: %d\n", nr_entries);

	/* Check the correctness of buffer */
	long state, state_trim, p_state_trim;
	long p_state = -1;
	bool ran = false, slept = false, slept_a_lot = false, runnable = false, stopped = false;
	int dead = 0, zombie = 0;

	int comm_test = 1;
	int pid_test = 1;
	int tid_test = 0;
	int sched_state_test = 1;
	int run_test, wait_test, exit_test, transition_test, duplication_test, clear_test;
	run_test = wait_test = exit_test = transition_test = duplication_test = 1;

	for (j = 0; j < nr_entries; j++)
		fprintf(stderr, "pid=%d tid=%d comm=%s state=%ld \n", buf[j].pid, buf[j].tid, buf[j].comm, buf[j].state);

	for (j = 0; j < nr_entries; j++)
	{
		state = buf[j].state;
		state_trim = state & 0xff;

		// Detect multiple threads
		if (!tid_test && buf[j].tid != buf[j].pid)
			tid_test = 1;

		if (comm_test && strcmp(buf[j].comm, basename(argv[0])) != 0)
		{
			fprintf(stderr, "INFO: unexpected comm '%s' (expected 'test') (pid = %d)\n", buf[j].comm, pid);
			comm_test = 0;
		}

		if (pid_test && buf[j].pid != pid)
		{
			fprintf(stderr, "INFO: unexpected pid '%d' (pid = %d)\n", buf[j].pid, pid);
			pid_test = 0;
		}

		if (buf[j].pid == buf[j].tid)
		{
			if (p_state != -1)
			{
				if (p_state == state)
				{
					fprintf(stderr, "INFO: detected duplicate state transition '%ld -> %ld' (pid = %d)\n", p_state, state, pid);
					duplication_test = 0;
				}

				if (state_trim == TASK_RUNNING && p_state_trim != TASK_RUNNABLE && p_state_trim != TASK_RUNNING ||
					state_trim == TASK_INTERRUPTIBLE && p_state_trim != TASK_RUNNING ||
					state_trim == TASK_UNINTERRUPTIBLE && p_state_trim != TASK_RUNNING ||
					state_trim == __TASK_STOPPED && p_state_trim != TASK_RUNNING ||
					state_trim == EXIT_ZOMBIE && p_state_trim != TASK_RUNNING ||
					state_trim == EXIT_DEAD && p_state_trim != EXIT_ZOMBIE)
				{
					fprintf(stderr, "INFO: detected unexpected state transition '%ld -> %ld' (pid = %d)\n", p_state, state, pid);
					transition_test = 0;
				}
			}
			p_state = buf[j].state;
			p_state_trim = p_state & 0xff;
		}

		if (state_trim != TASK_RUNNABLE && state_trim != TASK_RUNNING &&
			state_trim != TASK_INTERRUPTIBLE && state_trim != TASK_UNINTERRUPTIBLE && !(state_trim & __TASK_STOPPED) &&
			state_trim != EXIT_DEAD && state_trim != EXIT_ZOMBIE)
		{
			fprintf(stderr, "INFO: detected unexpected state value '%ld' (pid = %d)\n", state, pid);
			sched_state_test = 0;
		}
		ran = ran || state_trim == TASK_RUNNING;
		slept = slept || state_trim == TASK_INTERRUPTIBLE;
		runnable = runnable || state_trim == TASK_RUNNABLE;
		dead += state_trim == EXIT_DEAD;
		zombie += state_trim == EXIT_ZOMBIE;
		slept_a_lot = slept_a_lot || state_trim == TASK_UNINTERRUPTIBLE;
		stopped = stopped || state_trim & __TASK_STOPPED;
	}

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

	run_test = ran && runnable;
	wait_test = slept && slept_a_lot && stopped;
	exit_test = (dead == THREAD_NUM + 1) && (zombie == THREAD_NUM + 1);
	sched_state_test &= sched_state_test && ran && slept && runnable && exit_test && slept_a_lot && stopped;

	counter = 0;
	ret = syscall(__NR_SYSCALL_PSTRACE_GET, buf, &counter);
	if (ret < 0)
		fprintf(stderr, "err: %s\n", strerror(errno));
	clear_test = ret == 0;

	fprintf(stderr, "\n");
	// fprintf(stderr, "\nBasic Test 1: pstrace_get(buf, counter=0) nr_entries > 0\n");
	TEST(nr_entries > 0);
	// fprintf(stderr, "Basic Test 2: pstrace_get(buf, counter=0) tracks all threads in a thread group\n");
	TEST(tid_test);
	// fprintf(stderr, "Basic Test 3: pstrace_get(buf, counter=0) pstrace.comm should be correct\n");
	TEST(comm_test);
	// fprintf(stderr, "Basic Test 4: pstrace_get(buf, counter=0) pstrace.pid should be correct\n");
	TEST(pid_test);
	// fprintf(stderr, "Basic Test 5: pstrace_get(buf, counter=0) TASK_RUNNING / TASK_RUNNABLE detected\n");
	TEST(run_test);
	// fprintf(stderr, "Basic Test 6: pstrace_get(buf, counter=0) TASK_INTERRUPTIBLE / TASK_UNINTERRUPTIBLE / TASK_STOPPED detected\n");
	TEST(wait_test);
	// fprintf(stderr, "Basic Test 7: pstrace_get(buf, counter=0) correct number of EXIT_DEAD / EXIT_ZOMBIE detected\n");
	TEST(exit_test);
	// fprintf(stderr, "Basic Test 8: pstrace_get(buf, counter=0) all correct states detected\n");
	TEST(sched_state_test);
	// fprintf(stderr, "Basic Test 9: pstrace_get(buf, counter=0) duplication test\n");
	TEST(duplication_test);
	// fprintf(stderr, "Basic Test 10: pstrace_get(buf, counter=0) transition correctness test\n");
	TEST(transition_test);
	// fprintf(stderr, "Basic Test 11: after pstrace_clear, pstrace_get(buf, counter=0) should return 0\n");
	TEST(clear_test);

	return 0;
}