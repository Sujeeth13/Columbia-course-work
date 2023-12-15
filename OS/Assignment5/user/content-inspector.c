#include <stdio.h>
#include <stdlib.h>
#include <linux/shadowpt.h>
#include <string.h>

long shadowpt_page_contents(unsigned long pfn, void *buffer)
{
	printf("Calling with pfn: %lu\n", pfn);
	return syscall(NR_shadowpt_page_contents, pfn, buffer);
}

void pexit(char *s)
{
	perror(s);
	exit(EXIT_FAILURE);
}

int main(int argc, char **argv)
{
	unsigned long pfn;
	int index;
	char *out_buff;

	if (argc != 2) {
		printf("usage: %s <pfn>\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	pfn = strtoul(argv[1], NULL, 0);

	out_buff = malloc(PAGE_SIZE);
	if (!out_buff)
		pexit("malloc failed");

	if (shadowpt_page_contents(pfn, out_buff))
		pexit("shadowpt_page_contents failed");

	index = 0;
	for (int i = 0; i < 64; i++) {
		for (int j = 0; j < 64; j++) {
			printf("%02x ", (unsigned char)out_buff[index]);
			index++;
		}
		printf("\n");
	}

	free(out_buff);
	return 0;
}
