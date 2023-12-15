#include <stdio.h>
#include <linux/kernel.h>
#include <linux/tskinfo.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>


int main(int argc, char *argv[])
{
	int root_id = 0;
	if (argc >= 2) {
		char *endptr; // To store the end pointer for error checking
		root_id = strtol(argv[1], &endptr, 10);
		// Check for conversion errors
		if (*endptr != '\0' && *endptr != '\n') {
			printf("Conversion failed: %s is not a valid integer.\n", argv[1]);
			return 1;
		}
	}
	struct tskinfo *buf = NULL;
	int nr = 0;
	if (argc > 2) {
		char *endptr; // To store the end pointer for error checking
		nr = strtol(argv[2], &endptr, 10);
		// Check for conversion errors
		if (*endptr != '\0' && *endptr != '\n') {
			printf("Conversion failed: %s is not a valid integer.\n", argv[2]);
			return 1;
		}
	}

	const int BUFF_SIZE = 1024;
	buf = (struct tskinfo *)malloc(BUFF_SIZE * sizeof(struct tskinfo));
	int amma = syscall(451, buf, &nr, root_id);
	if (amma < 0) {
		fprintf(stderr, "Error: %s\n", strerror(errno));
		return 1;
	}
	for (int i = 0; i < BUFF_SIZE; i++) {
		if (strcmp(buf[i].comm, "") == 0)
			break;
		printf("%s,%d,%d,%d,%p,%p,%d\n", buf[i].comm, buf[i].pid, buf[i].tgid,
			buf[i].parent_pid, (void *)buf[i].userpc, (void *)buf[i].kernelpc, buf[i].level);
	}
	free(buf);
	return 0;
}
