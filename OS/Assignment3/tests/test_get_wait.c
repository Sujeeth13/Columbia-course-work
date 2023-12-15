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

#define TEST(cond) \
	printf("%s: " #cond "\n", (cond) ? "PASS" : "FAIL")

#define TASK_RUNNING			0x0000
#define TASK_INTERRUPTIBLE		0x0001
#define TASK_UNINTERRUPTIBLE	0x0002
#define TASK_RUNNABLE			0x0003
#define __TASK_STOPPED			0x0004
#define EXIT_DEAD				0x0010
#define EXIT_ZOMBIE				0x0020
#define TASK_WAKEKILL			0x0100
#define TASK_STOPPED			(TASK_WAKEKILL | __TASK_STOPPED)

#define BUF_SIZE 500
#define COUNTER_TEST 100
#define THREAD_NUM 15

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

void *target(void *arg)
{
	struct timespec time = { .tv_sec = 0, .tv_nsec = 100000000L}, time2;
	for (int i = 0; i < THREAD_NUM; i++)
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

static int check_buf(struct pstrace_entry *buf, long nr_entries, pid_t pid) {
	long state, state_trim, p_state_trim;
	long p_state = -1;
	int ret = 1;

	if (nr_entries <= 450)
		return 0;

	for (int j = 0; j < nr_entries; j++) {
		if (buf[j].pid != pid)
			continue;

		state = buf[j].state;
		state_trim = state & 0xff;
		fprintf(stderr, "pid=%d tid=%d comm=%s state=%ld \n", buf[j].pid, buf[j].tid, buf[j].comm, buf[j].state);

		if (buf[j].pid == buf[j].tid && buf[j].pid == pid) {
			if (p_state != -1) {
				if (p_state == state) {
					fprintf(stderr, "INFO: detected duplicate state transition '%ld -> %ld' (pid = %d)\n", p_state, state, pid);
					ret = 0;
				}

				if (state_trim == TASK_RUNNING && p_state_trim != TASK_RUNNABLE && p_state_trim != TASK_RUNNING ||
					state_trim == TASK_INTERRUPTIBLE && p_state_trim != TASK_RUNNING ||
					state_trim == TASK_UNINTERRUPTIBLE && p_state_trim != TASK_RUNNING ||
					state_trim == __TASK_STOPPED && p_state_trim != TASK_RUNNING ||
					state_trim == EXIT_ZOMBIE && p_state_trim != TASK_RUNNING ||
					state_trim == EXIT_DEAD && p_state_trim != EXIT_ZOMBIE
					) {
					fprintf(stderr, "INFO: detected unexpected state transition '%ld -> %ld' (pid = %d)\n", p_state, state, pid);
					ret = 0;
				}
			}
			p_state = buf[j].state;
			p_state_trim = p_state & 0xff;
		}

		if (state_trim != TASK_RUNNABLE && state_trim != TASK_RUNNING &&
			state_trim != TASK_INTERRUPTIBLE && state_trim != TASK_UNINTERRUPTIBLE && !(state_trim & __TASK_STOPPED) &&
			state_trim != EXIT_DEAD && state_trim != EXIT_ZOMBIE) {
			fprintf(stderr, "INFO: detected unexpected state value '%ld' (pid = %d)\n", state, pid);
			ret = 0;
		}
	}

	return ret;
}

void sig_handler(int x) { return; }

int main(int argc, char **argv)
{
	struct pstrace_entry buf[BUF_SIZE];
	int ret = 0, j = 0, nr_entries = 0;
	long counter = 0, init_counter;
	pid_t pid;
	int status;

	syscall(__NR_SYSCALL_PSTRACE_DISABLE);
	syscall(__NR_SYSCALL_PSTRACE_CLEAR);

	/* Get current counter */
	syscall(__NR_SYSCALL_PSTRACE_GET, buf, &counter);
	if (!counter)
		counter++;
	init_counter = counter;
	fprintf(stderr, "current counter %ld\n", counter);

	pid = fork();
	if (!pid) {
		sleep(2);
		do_wait_and_loop();
		return 0;
	} else if (pid < 0) {
		fprintf(stderr, "fork err: %s", strerror(errno));
		return -1;
	}

	fprintf(stderr, "pstrace_enable: %d\n", pid);
	ret = syscall(__NR_SYSCALL_PSTRACE_ENABLE, pid);
	if (ret)
		fprintf(stderr, "err: %s\n", strerror(errno));

	/* Set timer */
	signal(SIGALRM, sig_handler);
	alarm(6);

	fprintf(stderr, "pstrace_get: %d\n", pid);
	nr_entries = syscall(__NR_SYSCALL_PSTRACE_GET, buf, &counter);
	if (nr_entries < 0)
		fprintf(stderr, "err: %s\n", strerror(errno));
	fprintf(stderr, "current counter %ld\n", counter);
	
	waitpid(pid, NULL, 0);

	fprintf(stderr, "pstrace_disable\n");
	ret = syscall(__NR_SYSCALL_PSTRACE_DISABLE);
	if (ret)
		fprintf(stderr, "err: %s\n", strerror(errno));

	fprintf(stderr, "nr_entries: %d\n", nr_entries);

	for (j = 0; j < nr_entries; j++)
		fprintf(stderr, "pid=%d tid=%d comm=%s state=%ld \n", buf[j].pid, buf[j].tid, buf[j].comm, buf[j].state);

	int buf_test = nr_entries == 500 && check_buf(buf, nr_entries, pid);
	int counter_test = counter >= init_counter + BUF_SIZE;

	counter = 0;
	nr_entries = syscall(__NR_SYSCALL_PSTRACE_GET, buf, &counter);
	counter = counter - BUF_SIZE - COUNTER_TEST;
	nr_entries = syscall(__NR_SYSCALL_PSTRACE_GET, buf, &counter);
	int counter_test2 = nr_entries == BUF_SIZE - COUNTER_TEST;

	fprintf(stderr, "pstrace_clear\n");
	ret = syscall(__NR_SYSCALL_PSTRACE_CLEAR);
	if (ret)
		fprintf(stderr, "err: %s\n", strerror(errno));

	// fprintf(stderr, "Get Wait Test 1: pstrace_get(buf, counter > 0) gets full and correct buf\n");
	TEST(buf_test);
	// fprintf(stderr, "Get Wait Test 2: pstrace_get(buf, counter > 0) sets counter correctly\n");
	TEST(counter_test);
	// fprintf(stderr, "Get Wait Test 3: pstrace_get(buf, current_counter - BUF_SIZE - 100) return 400 nr_entries\n");
	TEST(counter_test2);

	return 0;
}
