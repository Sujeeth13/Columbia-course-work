#define _GNU_SOURCE
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <wait.h>
#include <time.h>
#include <pthread.h>
#include <stdbool.h>
#include <libgen.h>
#include <assert.h>
#include <stdio.h>

#define TEST(cond) \
	printf("%s: " #cond "\n", (cond) ? "PASS" : "FAIL")

#define BUF_SIZE 500
#define THREAD_NUM 30
int pressure_clear_test = 1;

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

void *get_noblock(void *arg)
{
    struct pstrace_entry buf[BUF_SIZE];
    long counter = 0;

    sleep(3);
    syscall(__NR_SYSCALL_PSTRACE_GET, buf, &counter);
    fprintf(stderr, "exiting noblock\n");

    pthread_exit(NULL);
}

void *get_block(void *arg)
{
    struct pstrace_entry buf[BUF_SIZE];
    long counter = (long) arg;
    fprintf(stderr, "calling block with counter %ld\n", counter);

    sleep(2);
    syscall(__NR_SYSCALL_PSTRACE_GET, buf, &counter);
    fprintf(stderr, "exiting block that had counter %ld\n", counter);

    pthread_exit(NULL);
}

void sig_handler(int x) { pressure_clear_test = 0; TEST(pressure_clear_test); }

int main(int argc, char **argv) {
    long i;
    long counter = 0;
    int pressure_test = 1;
    struct pstrace_entry buf[BUF_SIZE];
    pthread_t threads[THREAD_NUM];
    struct sigaction a;

    syscall(__NR_SYSCALL_PSTRACE_DISABLE);
    syscall(__NR_SYSCALL_PSTRACE_CLEAR);

    syscall(__NR_SYSCALL_PSTRACE_GET, buf, &counter);
    syscall(__NR_SYSCALL_PSTRACE_ENABLE, -1);

    for (i = 0; i < 2 * THREAD_NUM / 3; i++)
        pthread_create(&threads[i], NULL, &get_block, (void *) counter + (i + 10) * BUF_SIZE);

    for (; i < THREAD_NUM; i++)
        pthread_create(&threads[i], NULL, &get_noblock, NULL);

    sleep(5);
    // fprintf(stderr, "Pressure Test 1: pstrace_enable(-1) with multiple waiting pstrace_get won't freeze the kernel\n");
    TEST(pressure_test);

    signal(SIGALRM, sig_handler);
	alarm(6);
    syscall(__NR_SYSCALL_PSTRACE_DISABLE);
    syscall(__NR_SYSCALL_PSTRACE_CLEAR);
    for (int i = 0; i < THREAD_NUM; i++)
		pthread_join(threads[i], NULL);
    // fprintf(stderr, "Pressure Test 2: pstrace_enable(-1) pstrace_clear won't freeze the kernel\n");
    if (pressure_clear_test == 1)
        TEST(pressure_clear_test);
}
