#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <signal.h>
#include <linux/shadowpt.h>
#include <string.h>
#include <inttypes.h>


#define CONST_ADDR ((void *)(0x7F5B4AE00000))
#define CONST_SIZE (0x10000)

#define PREWRITE_MAPPING (0)

volatile sig_atomic_t exit_flag;

long shadowpt_enable(pid_t target, struct user_shadow_pt *dest)
{
	return syscall(NR_shadowpt_enable, target, dest);
}

long shadowpt_disable(void)
{
	return syscall(NR_shadowpt_disable);
}


void exit_flag_handler(int signum)
{
	exit_flag = 1;
}

void set_exit_handler(void)
{
	struct sigaction act;

	act.sa_handler = exit_flag_handler;
	sigaction(SIGTERM, &act, NULL);
	sigaction(SIGINT, &act, NULL);
	sigaction(SIGQUIT, &act, NULL);
	sigaction(SIGHUP, &act, NULL);
}

void pexit(char *s)
{
	perror(s);
	exit(EXIT_FAILURE);
}

void display_spte(struct shadow_pte *spte)
{
	char *allocated = "ALLOCD";
	char *unallocated = "UNALCD";

	char *anonymous = "ANONYM";
	char *file_backed = "FLBCKD";

	char *vaddr_writable = "VWRTBL";
	char *vaddr_readonly = "VRONLY";

	char *pte_mapped = "MAPPED";
	char *pte_unmapped = "UMAPPD";

	char *paddr_writable = "PWRTBL";
	char *paddr_readonly = "PRONLY";

	char *invalid = "------";

	char state_string[500];

	unsigned long state = spte_flags(spte);

	if (!is_vaddr_allocated(state)) {
		vaddr_writable = invalid;
		vaddr_readonly = invalid;
		anonymous = invalid;
		file_backed = invalid;
	}

	if (!is_pte_mapped(state)) {
		paddr_writable = invalid;
		paddr_readonly = invalid;
	}

	snprintf(state_string, 500, "%s,%s,%s,%s,%s",
			is_vaddr_allocated(state) ? allocated : unallocated,
			is_vaddr_anonymous(state) ? anonymous : file_backed,
			is_vaddr_writeable(state) ? vaddr_writable : vaddr_readonly,
			 is_pte_mapped(state) ? pte_mapped : pte_unmapped,
			is_pte_writeable(state) ? paddr_writable : paddr_readonly);

	printf("%#012" PRIxPTR "\t%#012" PRIxPTR "\t(%s)\t(%lu)",
		(uintptr_t) spte_vaddr(spte), (uintptr_t)(spte_pfn(spte)*PAGE_SIZE),
		state_string, state);
}

void display_shadowpt(struct user_shadow_pt *shadowpt, long max_pages)
{
	unsigned long start_vaddr = spt_start_vaddr(shadowpt);
	unsigned long end_vaddr = spt_end_vaddr(shadowpt);
	struct shadow_pte *spte;
	int line_count = 0;

	printf("Virtual\t\tPhysical\tFlags\t\t\t\t\tRaw State\n");
	for (unsigned long vaddr = start_vaddr; vaddr < end_vaddr; vaddr += getpagesize()) {
		if (line_count >= max_pages)
			break;

		spte = vaddr_to_spte(vaddr, shadowpt);
		display_spte(spte);
		printf("\n");

		line_count++;
	}
}

int main(int argc, char **argv)
{
	unsigned long start, end, num_vaddrs, mapping_size;
	pid_t pid;
	struct user_shadow_pt shadowpt;
	long ret;
	void *mapping;

	if (argc != 4 && argc != 2) {
		printf("usage: %s <target pid> <start address> <end_address>\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	pid = (pid_t) atoi(argv[1]);
	if (pid == 0)
		pid = getpid();

	if (argc == 4) {
		start = strtoul(argv[2], NULL, 0);
		end = strtoul(argv[3], NULL, 0);
	} else {
		start = (unsigned long) CONST_ADDR;
		end = (unsigned long) CONST_ADDR + CONST_SIZE;
	}

	num_vaddrs = end - start;

	printf("Tracking process %d\n", pid);
	printf("Tracking %lu vaddrs\n", num_vaddrs);

	mapping_size = spt_required_bytes(num_vaddrs);

	printf("Entries array requires %lu bytes\n", mapping_size);

	if (PREWRITE_MAPPING) { /* This should crash process, but not kernel */
		printf("Pre-writing to mapping, this should crash!\n");
		mapping = mmap(CONST_ADDR, mapping_size, PROT_READ|PROT_WRITE,
		MAP_ANONYMOUS|MAP_PRIVATE|MAP_FIXED, -1, 0);

		*((int *)mapping + 52) = 42;
		mprotect(mapping, mapping_size, PROT_READ);
	} else {
		mapping = mmap(CONST_ADDR, mapping_size, PROT_READ,
		MAP_ANONYMOUS|MAP_PRIVATE|MAP_FIXED, -1, 0);
	}
	if (!mapping)
		pexit("mmap failed");

	printf("Initializing shadowpt with mapping at: %p\n", mapping);
	printf("Tracking range %p through %p\n", (void *)start, (void *)(end));

	INIT_SHADOW_PT(&shadowpt, mapping, start, end);

	set_exit_handler();

	ret = shadowpt_enable(pid, &shadowpt);
	if (ret < 0) {
		perror("shadowpt_enable failed");
		goto CLEANUP_EXIT;
	}

	while (!exit_flag) {
		display_shadowpt(&shadowpt, 1000);
		sleep(1);
	}

	printf("\nPress enter to call shadowpt_disable\n");
	getchar();
	ret = shadowpt_disable();
	if (ret < 0)
		goto CLEANUP_EXIT;
	printf("Disabled, printing shadowpt:\n");
	display_shadowpt(&shadowpt, 1000);

	/* Confirm that we restored MAYWRITE flag properly */
	printf("Press enter to write '52' to mapping:");
	getchar();

	mprotect(mapping, mapping_size, PROT_WRITE);

	*(int *) mapping = 52;

	printf("Reading first byte of mapping: %d\n", *(int *) mapping);

	munmap(mapping, mapping_size);
	exit(0);

CLEANUP_EXIT:
	munmap(mapping, mapping_size);
	pexit("done");
}
