#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>

#define TEST(cond) \
	printf("%s: " #cond "\n", (cond) ? "PASS" : "FAIL")

#define BUF_SIZE 500

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

int main(int argc, char **argv)
{
	int ret, pstrace_disable_basic_test, pstrace_get_basic_test,
		pstrace_clear_basic_test;
	struct pstrace_entry buf[BUF_SIZE];
	long counter = 0;

	// fprintf(stderr, "Simple Test 1: pstrace_get should return 0\n");
	ret = syscall(__NR_SYSCALL_PSTRACE_GET, buf, &counter);
	pstrace_get_basic_test = ret == 0;
	TEST(pstrace_get_basic_test);

	// fprintf(stderr, "Simple Test 2: pstrace_clear should return 0\n");
	ret = syscall(__NR_SYSCALL_PSTRACE_CLEAR);
	pstrace_clear_basic_test = ret == 0;
	TEST(pstrace_clear_basic_test);

	// fprintf(stderr, "Simple Test 3: pstrace_disable should return 0\n");
	ret = syscall(__NR_SYSCALL_PSTRACE_DISABLE);
	pstrace_disable_basic_test = ret == 0;
	TEST(pstrace_disable_basic_test);

	return 0;
}
