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

#define THREAD_NUM 5

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
	for (size_t i = 0; i < length - 1; i++) {
		path[i] = 'a' + rand() % 26;
	}

	path[length - 1] = '\0';
}

int main(int argc, char **argv)
{
	pid_t pid;

	sleep(1);
	do_wait_and_loop();

	srand(time(NULL));

	for (int i = 0; i < 5; i++) {
		char path[16];
		random_path(path, sizeof(path));

		// clear cache
		pid = fork();
		if (!pid) {
			fprintf(stderr, "Clearing cache... ");
			int x = execl("./clear_cache.sh", "./clear_cache.sh", NULL);
			fprintf(stderr, "%d, %d\n", x, errno);
			if (x < 0)
				return -1;
		} else if (pid < 0) {
			fprintf(stderr, "fork failed\n");
			return -1;
		}

		waitpid(pid, NULL, 0);

		int fd = open(path, O_RDONLY);
		if (fd == -1) {
			perror(path);
		} else {
			fprintf(stderr, "Successfully opened %s\n", path);
			close(fd);
		}
	}

	// Flip between running and sleeping to fill up buffer
	struct timespec req, rem;
	req.tv_sec = 0;
	req.tv_nsec = 1000000L;

	for (int i = 0; i < 500; i++)
		nanosleep(&req, &rem);

	return 0;
}
