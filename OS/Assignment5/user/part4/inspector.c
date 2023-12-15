#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <fcntl.h>

#include <linux/shadowpt.h>

/**
 * You can confirm the value of this by checking the file at
 * `/proc/sys/kernel/pid_max`
 */
#define PID_MAX 4194304

#define PAGE_SIZE (4*1024)

extern long syscall(long number, ...);

long shadowpt_enable(int target, struct user_shadow_pt *dest)
{
	return syscall(451, target, dest);
}

int main(int argc, char *argv[])
{
	struct user_shadow_pt usp;
	long ret;
	int num_of_pages;
	pid_t target;
	int a;

	if (argc != 4) {
		printf("Give start and end address\n");
	}

	FILE *file = fopen("example.txt", "w");
	if (file == NULL) {
		perror("Error opening file");
		return 1; // or handle the error as needed
	}

	printf("hello!\n");

	num_of_pages = 2048;
	usp.entries = mmap(NULL, num_of_pages*sizeof(struct shadow_pte), PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	usp.start_vaddr = strtoul(argv[2], NULL, 0);
	usp.end_vaddr = strtoul(argv[3], NULL, 0);
	// usp.start_vaddr = 0xffffceab3000;
	// usp.end_vaddr = 0xffffceab5000;
	// num_of_pages = (usp.end_vaddr - usp.start_vaddr)/PAGE_SIZE;

	printf("Addr: %lu and %lu\n", usp.start_vaddr, usp.end_vaddr);
	printf("NUM_OF_PAGES: %d", num_of_pages);
	printf("running tests...\n");

	target = strtoul(argv[1], NULL, 0);
	ret = shadowpt_enable(target, &usp);
	if (ret < 0) {
		printf("FAIL: %d\n", __LINE__);
		perror(NULL);
		return 1;
	}
	scanf("%d", &a);
	// /* Printing the changes done in the kernel space */
	for (int entry = 0; entry < num_of_pages; entry++) {
		fprintf(file, "Entry %d : %lu and %lu and %lu\n", entry, usp.entries[entry].vaddr,
		 usp.entries[entry].pfn, usp.entries[entry].state_flags);
	}
	scanf("%d", &a);
	fprintf(file, "WRITING TO MEM---------------_____________------------------\n");
	for (int entry = 0; entry < num_of_pages; entry++) {
		fprintf(file, "Entry %d : %lu and %lu and %lu\n", entry, usp.entries[entry].vaddr,
		 usp.entries[entry].pfn, usp.entries[entry].state_flags);
	}
	scanf("%d", &a);
	fprintf(file, "MPROTECT---------------_____________------------------\n");
	for (int entry = 0; entry < num_of_pages; entry++) {
		fprintf(file, "Entry %d : %lu and %lu and %lu\n", entry, usp.entries[entry].vaddr,
		 usp.entries[entry].pfn, usp.entries[entry].state_flags);
	}
	scanf("%d", &a);
	fprintf(file, "MPROTECT AND WRITE---------------_____________------------------\n");
	for (int entry = 0; entry < num_of_pages; entry++) {
		fprintf(file, "Entry %d : %lu and %lu and %lu\n", entry, usp.entries[entry].vaddr,
		 usp.entries[entry].pfn, usp.entries[entry].state_flags);
	}
	scanf("%d", &a);
	fprintf(file, "FORK---------------_____________------------------\n");
	for (int entry = 0; entry < num_of_pages; entry++) {
		fprintf(file, "Entry %d : %lu and %lu and %lu\n", entry, usp.entries[entry].vaddr,
		 usp.entries[entry].pfn, usp.entries[entry].state_flags);
	}
	scanf("%d", &a);
	fprintf(file, "FORK AND WRITE---------------_____________------------------\n");
	for (int entry = 0; entry < num_of_pages; entry++) {
		fprintf(file, "Entry %d : %lu and %lu and %lu\n", entry, usp.entries[entry].vaddr,
		 usp.entries[entry].pfn, usp.entries[entry].state_flags);
	}
	scanf("%d", &a);
	fprintf(file, "MUNMAP---------------_____________------------------\n");
	for (int entry = 0; entry < num_of_pages; entry++) {
		fprintf(file, "Entry %d : %lu and %lu and %lu\n", entry, usp.entries[entry].vaddr,
		 usp.entries[entry].pfn, usp.entries[entry].state_flags);
	}
	scanf("%d", &a);
	fprintf(file, "EXIT---------------_____________------------------\n");
	for (int entry = 0; entry < num_of_pages; entry++) {
		fprintf(file, "Entry %d : %lu and %lu and %lu\n", entry, usp.entries[entry].vaddr,
		 usp.entries[entry].pfn, usp.entries[entry].state_flags);
	}
	fclose(file);
	return 0;
}
