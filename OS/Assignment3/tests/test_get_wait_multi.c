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
#define GET_WAIT_NUM 5
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

struct pstrace_entry buf[BUF_SIZE * GET_WAIT_NUM];
long counters[GET_WAIT_NUM];
long nr_entries[GET_WAIT_NUM];
long init_counter;

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

void *get_block(void *arg)
{
    long offset = (long) arg;
    long counter = init_counter;
    fprintf(stderr, "calling block %ld with counter %ld\n", offset, counter);
    nr_entries[offset] = syscall(__NR_SYSCALL_PSTRACE_GET, buf + offset * BUF_SIZE, &counter);
	counters[offset] = counter;
    fprintf(stderr, "exiting block %ld that had counter %ld\n", offset, counter);

	pthread_exit(NULL);
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

		if (buf[j].pid == buf[j].tid & buf[j].pid == pid) {
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

int main(int argc, char **argv)
{
	pthread_t threads[GET_WAIT_NUM];
	int ret = 0;
	long counter = 0, i;
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

	for (i = 0; i < GET_WAIT_NUM; i++)
		pthread_create(&threads[i], NULL, &get_block, (void *) i);
	
	waitpid(pid, NULL, 0);

	fprintf(stderr, "pstrace_disable\n");
	ret = syscall(__NR_SYSCALL_PSTRACE_DISABLE);
	if (ret)
		fprintf(stderr, "err: %s\n", strerror(errno));

	fprintf(stderr, "pstrace_clear\n");
	ret = syscall(__NR_SYSCALL_PSTRACE_CLEAR);
	if (ret)
		fprintf(stderr, "err: %s\n", strerror(errno));

	for (i = 0; i < GET_WAIT_NUM; i++)
		pthread_join(threads[i], NULL);

	// fprintf(stderr, "Get Wait Multi Test 1: multiple pstrace_gets(buf, counter > 0) all wake up\n");
	int wake_up_test = 1;
	TEST(wake_up_test);

	// fprintf(stderr, "Get Wait Multi Test 2: multiple pstrace_gets(buf, counter > 0) all get full and correct buf\n");
	int buf_test = 1;
	for (i = 0; i < GET_WAIT_NUM; i++) {
		if (!check_buf(buf + i * BUF_SIZE, nr_entries[i], pid)) {
			buf_test = 0;
			break;
		}
	}
	TEST(buf_test);
	// fprintf(stderr, "Get Wait Multi Test 3: multiple pstrace_gets(buf, counter > 0) all set counter correctly\n");
	int counter_test = 1;
	for (i = 0; i < GET_WAIT_NUM; i++) {
		if (counters[i] < init_counter + BUF_SIZE) {
			counter_test = 0;
			break;
		}
	}
	TEST(counter_test); 

	return 0;
}
