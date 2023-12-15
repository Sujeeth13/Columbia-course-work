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
#include <signal.h>

#define TEST(cond) \
	printf("%s: " #cond "\n", (cond) ? "PASS" : "FAIL")

#define BUF_SIZE 500
#define THREAD_NUM 3

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

pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
pid_t pid;
int nr_entries = -1, updated_counter = -1, wake_up = 0, ret_val_test = 0, counter_test = 0;
struct pstrace_entry buf[BUF_SIZE * THREAD_NUM];

void *pstrace_get_wait(void *arg)
{
	long counter = 1000000, i = (long)arg, ret;

	fprintf(stderr, "[Thread %ld] pstrace_get %d %ld waiting...\n", i, pid, counter);
	ret = syscall(__NR_SYSCALL_PSTRACE_GET, buf + i * BUF_SIZE, &counter);
	if (ret < 0)
		fprintf(stderr, "err: %s\n", strerror(errno));
	fprintf(stderr, "[Thread %ld] pstrace_get %d %ld finish, ret=%ld\n", i, pid, counter, ret);
	pthread_mutex_lock(&lock);
	wake_up++;
	if (nr_entries == -1) {
		ret_val_test = 1;
		nr_entries = ret;
	} else if (ret != nr_entries) {
		ret_val_test = 0;
	}

	if (updated_counter == -1) {
		counter_test = 1;
		updated_counter = counter;
	} else if (updated_counter != counter) {
		counter_test = 0;
	}
	pthread_mutex_unlock(&lock);
	pthread_exit(NULL);
}

void *create_threads(void *arg)
{
	pthread_t threads[THREAD_NUM];
	long i;

	for (i = 0; i < THREAD_NUM; i++)
		pthread_create(&threads[i], NULL, &pstrace_get_wait, (void *)i);

	for (i = 0; i < THREAD_NUM; i++)
		pthread_join(threads[i], NULL);

	pthread_exit(NULL);
}

void *target(void *arg)
{
	struct timespec time = { .tv_sec = 0, .tv_nsec = 250000000L}, time2;
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

int main(int argc, char **argv)
{
	int ret, clear_wake_up_test;
	pthread_t pid2;

	pid = fork();
	if (!pid) {
		sleep(1);
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
	
	pthread_create(&pid2, NULL, &create_threads, NULL);

	// Wait for pid to finish
	sleep(3);

	fprintf(stderr, "pstrace_disable\n");
	ret = syscall(__NR_SYSCALL_PSTRACE_DISABLE);
	if (ret)
		fprintf(stderr, "err: %s\n", strerror(errno));

	fprintf(stderr, "pstrace_clear\n");
	ret = syscall(__NR_SYSCALL_PSTRACE_CLEAR);
	if (ret)
		fprintf(stderr, "err: %s\n", strerror(errno));

	sleep(2);

	clear_wake_up_test = wake_up == THREAD_NUM;
	// fprintf(stderr, "\nClear Wakeup Test 1: pstrace_clear should wake up all waiting pstrace_gets\n");
	TEST(clear_wake_up_test);
	// fprintf(stderr, "\nClear Wakeup Test 2: woken up pstrace_gets should set counter correctly\n");
	TEST(counter_test);
	// fprintf(stderr, "\nClear Wakeup Test 3: woken up pstrace_gets return values are correct\n");
	TEST(ret_val_test);

	return 0;
}
