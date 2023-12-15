#include <stdio.h>
#include <linux/kernel.h>
#include <linux/tskinfo.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <pthread.h>

void *dummy(void *ptr)
{ /*dummy function the thread executes*/
	while (1)
		;
	return NULL;
}

void make_process_tree(void)
{ /* makes the requested process tree*/
	pid_t pid1, pid2, pid3, pid4;
	pthread_t thread;
	sleep(2);
	printf("Process 1: %d\n", getpid());
	pthread_create(&thread, NULL, dummy, NULL);
	pid1 = fork();

	if (pid1 < 0) {
		perror("fork failed");
		exit(1);
	}

	if (pid1 == 0) {  /*Child 5002*/
		/*This child can be used to spawn another child: 5003*/
		pid3 = fork();
		if (pid3 == 0) { /*Child 5004*/
			while (1)
				;
			exit(0);
		}
		wait(NULL);
		exit(0);  /*Exit child 5002*/
	} else {
		pid2 = fork();
		if (pid2 < 0) {
			perror("fork failed");
			exit(1);
		}

		if (pid2 == 0) {  /*Child 5003*/
			/*This child can be used to spawn another child: 5004*/
			sleep(2);
			pid4 = fork();
			if (pid4 == 0) { /*Child 5005*/
				while (1)
					;
				exit(0);
			}
			wait(NULL);
			exit(0);  // Exit child 5003
		}
	}

	wait(NULL);
	wait(NULL);
	pthread_join(thread, NULL);
	return;
}

int main(void)
{
	pid_t pid;
	pid = getpid();
	while (pid != 4999) { /* loopning till we can make a process with pid 5000*/
		pid = fork();
		if (pid == 0)
			exit(1);
	}
	pid = fork();
	if (pid < 0) {
		perror("fork failed");
		exit(1);
	} else if (pid == 0) { /*Process 5000 makes the desired tree*/
		make_process_tree();
	} else { /*parent of process 5000 exits so that process 5000's parent can be process 1*/
		sleep(2);
		return 0;
	}
	return 0;
}
