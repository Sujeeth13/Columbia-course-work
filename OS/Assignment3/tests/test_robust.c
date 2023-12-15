#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

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
	int ret, pstrace_enable_robust_test, pstrace_get_null_test,
		pstrace_get_malicious_test, pstrace_get_negative_test;
	struct pstrace_entry buf[BUF_SIZE];
	long counter = -1;

	// fprintf(stderr, "Robust Test 1: pstrace_enable (pid = -2795) should return non zero and set errno to ESRCH / EINVAL\n");
	ret = syscall(__NR_SYSCALL_PSTRACE_ENABLE, -2795);
	pstrace_enable_robust_test = ret != 0 && (errno == ESRCH || errno == EINVAL);
	TEST(pstrace_enable_robust_test);

	// fprintf(stderr, "Robust Test 2: pstrace_get (null pointer) should return non zero and set errno to EINVAL\n");
	ret = syscall(__NR_SYSCALL_PSTRACE_GET, NULL, NULL);
	pstrace_get_null_test = ret != 0 && errno == EINVAL;
	TEST(pstrace_get_null_test);

	// fprintf(stderr, "Robust Test 3: pstrace_get (counter < 0) should return non zero and set errno to EINVAL\n");
	ret = syscall(__NR_SYSCALL_PSTRACE_GET, buf, &counter);
	pstrace_get_negative_test = ret != 0 && errno == EINVAL;
	TEST(pstrace_get_negative_test);

	// fprintf(stderr, "Robust Test 4: pstrace_get (non-readable/writable address) should return non zero and set errno to EFAULT\n");
	ret = syscall(__NR_SYSCALL_PSTRACE_GET, 0x1000, 0x1000);
	pstrace_get_malicious_test = ret != 0 && errno == EFAULT;
	TEST(pstrace_get_malicious_test);

	return 0;
}
