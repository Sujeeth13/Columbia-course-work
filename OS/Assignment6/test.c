#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>

void readInvalidOffset(const char *filename)
{
	FILE *file = fopen(filename, "r");
	if (file == NULL) {
		perror("Error opening file");
		return;
	}

	// Seeking to an invalid offset (e.g., far beyond the end of the file)
	if (fseek(file, 10000, SEEK_SET) != 0) {
		perror("Error seeking in file");
	} else {
		char buffer[100];
		if (fread(buffer, sizeof(char), sizeof(buffer), file) > 0) {
			printf("Data read from invalid offset: %s\n", buffer);
		} else {
			printf("No data read from invalid offset.\n");
		}
	}

	fclose(file);
}

void listInvalidDirectoryEntry(const char *path)
{
	DIR *dir = opendir(path);
	if (dir == NULL) {
		perror("Error opening directory");
		return;
	}

	struct dirent *entry;
	int count = 0;

	// Attempting to read past the end of the directory entries
	while ((entry = readdir(dir)) != NULL) {
		count++;
	}

	// Trying to access an invalid entry
	if (count > 0) {
		printf("Attempting to access invalid directory entry.\n");
		entry = readdir(dir); // This should return NULL
		if (entry == NULL) {
		    printf("Invalid directory entry is NULL as expected.\n");
		} else {
		    printf("Invalid directory entry: %s\n", entry->d_name);
		}
	} else {
		printf("Directory is empty.\n");
	}

	closedir(dir);
}

int main(void)
{
	readInvalidOffset("/mnt/ez/hello.txt"); // Replace with your file path
	listInvalidDirectoryEntry("/mnt/ez"); // Replace with your directory path
	return 0;
}
