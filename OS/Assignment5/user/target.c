#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <string.h>
#include <fcntl.h>


#define CONST_ADDR ((void *)(0x7F5B4AE01000))
#define CONST_FILENAME "randfile"

#define BIG_STACK_SIZE (1<<15)

void *get_stack_top(void)
{
	FILE *file;
	char *line = NULL;
	size_t len = 0;
	ssize_t read;
	unsigned long start;

	file = fopen("/proc/self/maps", "r");
	if (file == NULL) {
		perror("Error opening file");
		return NULL;
	}

	while ((read = getline(&line, &len, file)) != -1) {
		if (strstr(line, "[stack]") != NULL) {
			line[12] = '\0';
			start = strtoul(line, NULL, 16);
			fclose(file);
			free(line);
			return (void *) start;
		}
	}

	fclose(file);

	if (line)
		free(line);


	return NULL;
}

void grow_stack_big(void)
{
	int test[BIG_STACK_SIZE];

	printf("New stack pointer: %p\n", get_stack_top());
	printf("Press enter to write to stack array\n");
	getchar();

	for (size_t i = 0; i < BIG_STACK_SIZE; i++)
		test[i] = rand();

	printf("Press enter to exit bigstack function (test first byte %d)\n", test[0]);
	getchar();
}

void test_stack_growth(void)
{
	void *stack_top = get_stack_top();

	printf("Rough stack pointer: %p (range to monitor: %p %p)\n", stack_top,
		stack_top - 4*(1<<13), stack_top + 2*(1<<13));

	printf("Press enter to grow stack\n");
	getchar();

	grow_stack_big();
}

void test_mprotect(char *mapping, int pages)
{
		printf("Press enter to change mapping to read only\n");
		getchar();

		mprotect(mapping, pages*4096, PROT_READ);

		printf("Press enter to change mapping to writable\n");
		getchar();

		mprotect(mapping, pages*4096, PROT_READ|PROT_WRITE);
}

void test_reads_writes(char *mapping, int pages)
{
	int i;
	char *curr;

	for (i = 0; i < (pages); i++) {
		curr = mapping + i*4096;
		printf("Press enter to read from page at %p\n", curr);
		getchar();

		printf("First value in page: %d\n", (int) *curr);

		printf("Press enter to write to page at %p\n", curr);
		getchar();

		memset(curr, 0xDB, 500);
	}
}


void test_brk(int pages)
{
	void *brkpoint;

	brkpoint = sbrk(0);

	printf("Breakpoint: %p\n", brkpoint);

	printf("Press Enter to increase breakpoing\n");
	getchar();

	brkpoint = sbrk(pages*4096);
	test_reads_writes(brkpoint, pages);

	printf("Press enter to decrease breakpoint\n");
	getchar();
	brk(brkpoint - pages*4096);
}


/* Not required to work for assignment */
void test_huge_mapping(int pages)
{
	char *mapping;

	printf("Press enter to map %d huge pages\n", pages);
	getchar();

	posix_memalign((void **)&mapping, (1 << 21), pages*(1 << 21));
	printf("Mapping starts at %p\n", mapping);

	printf("Press enter to madvise\n");
	getchar();
	madvise(mapping, (1 << 21), MADV_HUGEPAGE);

	printf("Press enter to write\n");
	getchar();
	memset(mapping, 10, 10);

	printf("Press enter to change mapping to read only\n");
	getchar();

	mprotect(mapping, pages*(1 << 21), PROT_READ);

	printf("Press enter to free mapping\n");
	getchar();

	free(mapping);
}


void test_filebacked_mapping(int pages, char *filepath, int flags)
{
	char *mapping;
	int fd;

	printf("Press enter to mmap %d filebacked pages\n", pages);
	getchar();

	fd = open(filepath, O_RDWR);
	if (fd < 0) {
		perror("open failed");
		exit(1);
	}

	mapping = mmap(CONST_ADDR, pages*4096, PROT_READ | PROT_WRITE,
		MAP_FIXED | flags, fd, 0);
	if (mapping == MAP_FAILED) {
		perror("mmap failed\n");
		exit(1);
	}
	if (mapping != CONST_ADDR) {
		fprintf(stderr, "Couldn't get constant address,got %p\n", mapping);
		exit(1);
	}

	test_reads_writes(mapping, pages);

	test_mprotect(mapping, pages);

	printf("Press enter to unmap pages\n");
	getchar();

	munmap(mapping, pages*4096);
	close(fd);
}

void test_anonymous_mapping(int pages, int flags)
{
	char *anon;

	printf("Press enter to mmap %d anonymous pages\n", pages);
	getchar();
	anon = mmap(CONST_ADDR, pages*4096, PROT_READ | PROT_WRITE,
		MAP_FIXED | flags | MAP_ANONYMOUS, -1, 0);
	if (anon == MAP_FAILED) {
		perror("mmap failed");
		exit(1);
	}
	if (anon != CONST_ADDR) {
		fprintf(stderr, "Couldn't get constant address\n");
		exit(1);
	}

	test_reads_writes(anon, pages);
	test_mprotect(anon, pages);

	printf("Press enter to unmap range\n");
	getchar();

	munmap(anon, pages*4096);

}

int main(int argc, char **argv)
{
	int pages;
	char *filepath;
	char *map_anon, *map_shared;
	pid_t pid;


	if (argc != 3 && argc != 1) {
		printf("Usage: %s <pages> <filepath>", *argv);
		exit(1);
	}
	printf("PID: %d\n", getpid());

	if (argc == 1) {
		pages = 5;
		filepath = CONST_FILENAME;
	} else {
		pages = atoi(argv[1]);
	}
	printf("Testing Stack Growth\n");
	test_stack_growth();

	printf("Testing brk\n");
	test_brk(pages);

	printf("Testing Anon Mapping\n");
	test_anonymous_mapping(pages, MAP_PRIVATE);

	printf("Testing private fileback mapping\n");
	test_filebacked_mapping(pages, filepath, MAP_PRIVATE);

	printf("Testing shared fileback mapping\n");
	test_filebacked_mapping(pages, filepath, MAP_SHARED);

	printf("Testing huge page mapping\n");
	test_huge_mapping(1);

	/* Test fork */
	printf("Press enter to mmap half private, half shared\n");
	getchar();

	map_anon = mmap(CONST_ADDR, (pages/2)*4096, PROT_READ | PROT_WRITE,
		MAP_FIXED|MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
	map_shared = mmap(CONST_ADDR + (pages/2)*4096, (pages - pages/2)*4096,
		PROT_READ|PROT_WRITE, MAP_FIXED|MAP_ANONYMOUS|MAP_SHARED, -1, 0);

	if (map_anon == MAP_FAILED || map_shared == MAP_FAILED) {
		perror("mmap failed");
		exit(1);
	}

	test_reads_writes(map_anon, pages);

	printf("Press enter to fork\n");
	getchar();

	pid = fork();
	if (pid == 0) {
		sleep(5);
		printf("Child exiting\n");
		exit(0);
	}

	test_reads_writes(map_anon, pages);

	printf("Press enter to exec\n");
	getchar();
	execv("./target", argv);
}
