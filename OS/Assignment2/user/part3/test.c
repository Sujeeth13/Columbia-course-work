#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdbool.h>

/*
 * Including tskinfo.h requires adding a tskinfo.h header to include/uapi/ and
 * installing headers with the command (from your linux/ directory):
 * "make headers_install INSTALL_HDR_PATH=/usr"
 * Alternatively, you can also just re-define the tskinfo struct somewhere in
 * userspace.
 */
#include <linux/tskinfo.h>

#define __NR_ptree 451
#define DEFAULT_NR 10
#define DEFAULT_ROOT_PID 0

static void print_tskinfo(const struct tskinfo *buf, const int nr, int debug);


int ptree(struct tskinfo *buf, int *nr, int root_pid)
{
	return syscall(__NR_ptree, buf, nr, root_pid);
}


int main(int argc, char **argv)
{
	/* This is not good practice, should make the debug flag an argument */
	int debug = 0;

	if (argc > 2) {
		fprintf(stderr, "error: %s\n", "too many arguments");
		return 1;
	}

	int nr = DEFAULT_NR;
	int root_pid = DEFAULT_ROOT_PID;

	if (argc == 2) {
		char *endptr;
		unsigned long val = strtoul(argv[1], &endptr, 10);

		if (*endptr != '\0' || val > INT_MAX) {
			fprintf(stderr, "error: %s\n", "invalid argument");
			return 1;
		}

		root_pid = val;
	}

	struct tskinfo *buf = NULL;
	int ret = 0;
	int allocated = nr;

	while (nr == allocated) {
		free(buf);
		nr = nr * 2;
		allocated = nr;
		buf = malloc(sizeof(struct tskinfo) * nr);

		if (!buf) {
			fprintf(stderr, "error: %s\n", strerror(errno));
			return 1;
		}

		ret = ptree(buf, &nr, root_pid);
		if (ret < 0) {
			fprintf(stderr, "error: %s\n", strerror(errno));
			free(buf);
			return 1;
		}

	}
	if (debug)
		printf("ptree returned %d\n", ret);
	print_tskinfo(buf, nr, debug);
	free(buf);
	return 0;
}

void print_tskinfo(const struct tskinfo *buf, const int nr, int debug)
{
	int i;

	if (buf == NULL || nr < 1)
		return;

	for (i = 0; i < nr; ++i) {
		if (debug) {
			printf("%s,pid:%d,tgid:%d,ppid: %d,userpc: %p,kernelpc:%p,lvl: %d\n",
				buf[i].comm, buf[i].pid, buf[i].tgid, buf[i].parent_pid,
				(void *)buf[i].userpc, (void *)buf[i].kernelpc, buf[i].level);
		} else {
			printf("%s,%d,%d,%d,%p,%p, %d\n", buf[i].comm, buf[i].pid,
				buf[i].tgid, buf[i].parent_pid, (void *)buf[i].userpc,
				(void *)buf[i].kernelpc, buf[i].level);
		}
	}
}
