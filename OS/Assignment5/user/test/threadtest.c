#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdbool.h>
#include <unistd.h>

#include <sys/syscall.h>

#define NUM_THREADS 5

// Test function that always returns false
bool test(void)
{
	return syscall(454);
}

// Thread function
void *perform_work(void *argument)
{
	int passed_in_value;

	passed_in_value = *((int *)argument);
	printf("Hello from thread! Thread id: %d\n", passed_in_value);

	sleep(3);
	printf("Bye from thread! Thread id: %d\n", passed_in_value);
	return NULL;
}

int main(void)
{
	pthread_t threads[NUM_THREADS];
	int thread_args[NUM_THREADS];
	int result_code, index;

	// Create all threads one by one
	for (index = 0; index < NUM_THREADS; ++index) {
		thread_args[index] = index;
		printf("In main: creating thread %d\n", index);
		result_code = pthread_create(&threads[index], NULL, perform_work, &thread_args[index]);
		// Check for creation errors
		if (result_code) {
			printf("ERROR; return code from pthread_create() is %d\n", result_code);
			exit(-1);
		}
	}

	// Wait for each thread to complete
	for (index = 0; index < NUM_THREADS; ++index) {
		result_code = pthread_join(threads[index], NULL);
		printf("In main: Thread %d has exited\n", index);
		if (!test()) {
			printf("Test after thread %d exits: FALSE\n", index);
		} else {
			printf("Test after thread %d exits: TRUE\n", index);
		}
		// Check for join errors
		if (result_code) {
			printf("ERROR; return code from pthread_join() is %d\n", result_code);
			exit(-1);
		}
	}

	printf("In main: All threads have completed\n");

	return 0;
}

