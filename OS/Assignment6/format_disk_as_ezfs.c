#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>

/* These are the same on a 64-bit architecture */
#define timespec64 timespec
#define IMG_BLK 8
#define TXT_BLK 2

#include "ezfs.h"


void passert(int condition, char *message)
{
	printf("[%s] %s\n", condition ? " OK " : "FAIL", message);
	if (!condition)
		exit(1);
}

void inode_reset(struct ezfs_inode *inode)
{
	struct timespec current_time;

	/* In case inode is uninitialized/previously used */
	memset(inode, 0, sizeof(*inode));
	memset(&current_time, 0, sizeof(current_time));

	/* These sample files will be owned by the first user and group on the system */
	inode->uid = 1000;
	inode->gid = 1000;

	/* Current time UTC */
	clock_gettime(CLOCK_REALTIME, &current_time);
	inode->i_atime = inode->i_mtime = inode->i_ctime = current_time;
}

void dentry_reset(struct ezfs_dir_entry *dentry)
{
	memset(dentry, 0, sizeof(*dentry));
}

int main(int argc, char *argv[])
{
	int i, fd, fp, disk_blks;
	ssize_t ret, pret, bret, len;
	struct ezfs_super_block sb;
	struct ezfs_inode inode;
	struct ezfs_dir_entry dentry;

	char *hello_contents = "Hello world!\n";
	char *names_contents = "Emma Nieh; Zijian Zhang; Haruki Gonai\n";
	char buf[EZFS_BLOCK_SIZE], pbuf[EZFS_BLOCK_SIZE * IMG_BLK];
	char bbuf[EZFS_BLOCK_SIZE * TXT_BLK];
	uint64_t indirect_contents[IMG_BLK-1];
	const char zeroes[EZFS_BLOCK_SIZE] = { 0 };

	if (argc != 3) {
		printf("Usage: ./format_disk_as_ezfs DEVICE_NAME DISK_BLKS.\n");
		return -1;
	}
	disk_blks = atoi(argv[2]);
	if (disk_blks <= 0) {
		printf("Invalid DISK_BLKS.\n");
		return -1;
	}

	fd = open(argv[1], O_RDWR);
	if (fd == -1) {
		perror("Error opening the device");
		return -1;
	}
	memset(&sb, 0, sizeof(sb));

	fp = open("./big_files/big_img.jpeg", O_RDWR);
	if (fp == -1) {
		perror("Error opening the image");
		return -1;
	}
	pret = read(fp, pbuf, EZFS_BLOCK_SIZE * IMG_BLK);
	close(fp);
	passert(pret != -1, "Read big img contents");

	fp = open("./big_files/big_txt.txt", O_RDWR);
	if (fp == -1) {
		perror("Error opening the txt");
		return -1;
	}
	bret = read(fp, bbuf, EZFS_BLOCK_SIZE * 2);
	close(fp);
	passert(bret != -1, "Read big txt contents");

	sb.version = 1;
	sb.magic = EZFS_MAGIC_NUMBER;
	sb.disk_blks = disk_blks;

	/* 1. inode1 and direct_blk_n 2 are taken by the root
	 * 2. inode2 and direct_blk_n 3 are taken by hello.txt
	 * 3. inode3 and direct_blk_n 4 are taken by subdir
	 * 4. inode4 and direct_blk_n 5 are taken by subdir/names.txt
	 * 5. inode5 and direct_blk_n 6-13 are taken by subdir/big_img.jpeg
	 * 6. inode6 and direct_blk_n 14-15 are taken by subdir/big_txt.txt
	 * Mark them as such.
	 */
	for (i = 0; i < 6; ++i)
		SETBIT(sb.free_inodes, i);

	for (i = 0; i < 15; ++i)
		SETBIT(sb.free_data_blocks, i);

	/* Write the superblock to the first block of the filesystem. */
	ret = write(fd, (char *)&sb, sizeof(sb));
	passert(ret == EZFS_BLOCK_SIZE, "Write superblock");

	inode_reset(&inode);
	inode.mode = S_IFDIR | 0777;
	inode.nlink = 3; // add 1 to 2 because add another directory
	inode.direct_blk_n = EZFS_ROOT_DATABLOCK_NUMBER;
	inode.indirect_blk_n = 0;
	inode.file_size = EZFS_BLOCK_SIZE;
	inode.nblocks = 1;

	/* Write the root inode starting in the second block. */
	ret = write(fd, (char *)&inode, sizeof(inode));
	passert(ret == sizeof(inode), "Write root inode");

	/* The hello.txt file will take inode num following root inode num. */
	inode_reset(&inode);
	inode.nlink = 1;
	inode.mode = S_IFREG | 0666;
	inode.direct_blk_n = EZFS_ROOT_DATABLOCK_NUMBER + 1;
	inode.indirect_blk_n = 0;
	inode.file_size = strlen(hello_contents);
	inode.nblocks = 1;

	ret = write(fd, (char *) &inode, sizeof(inode));
	passert(ret == sizeof(inode), "Write hello.txt inode");

	/* sub directory will take inode num following hello.txt inode num. */
	inode_reset(&inode);
	inode.mode = S_IFDIR | 0777;
	inode.nlink = 2;
	inode.direct_blk_n = EZFS_ROOT_DATABLOCK_NUMBER + 2;
	inode.indirect_blk_n = 0;
	inode.file_size = EZFS_BLOCK_SIZE;
	inode.nblocks = 1;

	/* Write subdir inode */
	ret = write(fd, (char *)&inode, sizeof(inode));
	passert(ret == sizeof(inode), "Write subdir inode");

	/* The names.txt file will take inode num following subdir inode num. */
	inode_reset(&inode);
	inode.nlink = 1;
	inode.mode = S_IFREG | 0666;
	inode.direct_blk_n = EZFS_ROOT_DATABLOCK_NUMBER + 3;
	inode.indirect_blk_n = 0;
	inode.file_size = strlen(names_contents);
	inode.nblocks = 1;

	ret = write(fd, (char *) &inode, sizeof(inode));
	passert(ret == sizeof(inode), "Write names.txt inode");

	/* The big_img.jpeg file will take inode num following names.txt inode num. */
	inode_reset(&inode);
	inode.nlink = 1;
	inode.mode = S_IFREG | 0666;
	inode.direct_blk_n = EZFS_ROOT_DATABLOCK_NUMBER + 4;
	inode.indirect_blk_n = EZFS_ROOT_DATABLOCK_NUMBER + 6 + IMG_BLK;
	inode.file_size = pret;
	inode.nblocks = IMG_BLK;

	ret = write(fd, (char *) &inode, sizeof(inode));
	passert(ret == sizeof(inode), "Write big_img.jpeg inode");

	/* The big_txt.txt file will take inode num following last inode=big_img.jpeg. */
	inode_reset(&inode);
	inode.nlink = 1;
	inode.mode = S_IFREG | 0666;
	inode.direct_blk_n = EZFS_ROOT_DATABLOCK_NUMBER + 4 + IMG_BLK;
	inode.indirect_blk_n = EZFS_ROOT_DATABLOCK_NUMBER + 7 + IMG_BLK;
	inode.file_size = bret;
	inode.nblocks = TXT_BLK;

	ret = write(fd, (char *) &inode, sizeof(inode));
	passert(ret == sizeof(inode), "Write big_txt.txt inode");

	/* lseek to the next data block */
	ret = lseek(fd, EZFS_BLOCK_SIZE - 6 * sizeof(struct ezfs_inode),
		SEEK_CUR);
	passert(ret >= 0, "Seek past inode table");

	/* dentry for hello.txt */
	dentry_reset(&dentry);
	strncpy(dentry.filename, "hello.txt", sizeof(dentry.filename));
	dentry.active = 1;
	dentry.inode_no = EZFS_ROOT_INODE_NUMBER + 1;

	ret = write(fd, (char *) &dentry, sizeof(dentry));
	passert(ret == sizeof(dentry), "Write dentry for hello.txt");

	/* dentry for subdir */
	dentry_reset(&dentry);
	strncpy(dentry.filename, "subdir", sizeof(dentry.filename));
	dentry.active = 1;
	dentry.inode_no = EZFS_ROOT_INODE_NUMBER + 2;

	ret = write(fd, (char *) &dentry, sizeof(dentry));
	passert(ret == sizeof(dentry), "Write dentry for subdir");

	/* two entries: so 2 * sizeof() */
	len = EZFS_BLOCK_SIZE - 2 * sizeof(struct ezfs_dir_entry);
	ret = write(fd, zeroes, len);
	passert(ret == len, "Pad to end of root dentries");

	/* hello.txt contents */
	len = strlen(hello_contents);
	strncpy(buf, hello_contents, len);
	ret = write(fd, buf, len);
	passert(ret == len, "Write hello.txt contents");

	/* lseek to the next file block */
	ret = lseek(fd, EZFS_BLOCK_SIZE - len, SEEK_CUR);
	passert(ret >= 0, "Seek to next file block");

	/* names.txt dentry */
	dentry_reset(&dentry);
	strncpy(dentry.filename, "names.txt", sizeof(dentry.filename));
	dentry.active = 1;
	dentry.inode_no = EZFS_ROOT_INODE_NUMBER + 3;

	ret = write(fd, (char *) &dentry, sizeof(dentry));
	passert(ret == sizeof(dentry), "Write dentry for names.txt");

	/* big_img.jpeg dentry */
	dentry_reset(&dentry);
	strncpy(dentry.filename, "big_img.jpeg", sizeof(dentry.filename));
	dentry.active = 1;
	dentry.inode_no = EZFS_ROOT_INODE_NUMBER + 4;

	ret = write(fd, (char *) &dentry, sizeof(dentry));
	passert(ret == sizeof(dentry), "Write dentry for big_img.jpeg");

	/* big_txt.txt dentry */
	dentry_reset(&dentry);
	strncpy(dentry.filename, "big_txt.txt", sizeof(dentry.filename));
	dentry.active = 1;
	dentry.inode_no = EZFS_ROOT_INODE_NUMBER + 5;

	ret = write(fd, (char *) &dentry, sizeof(dentry));
	passert(ret == sizeof(dentry), "Write dentry for big_txt.txt");

	/* end subdir: contains names.txt, big_img.jpeg, big_txt.txt */
	len = EZFS_BLOCK_SIZE - 3 * sizeof(struct ezfs_dir_entry);
	ret = write(fd, zeroes, len);
	passert(ret == len, "Pad to end of subdir dentries");

	/* names.txt contents */
	len = strlen(names_contents);
	strncpy(buf, names_contents, len);
	ret = write(fd, buf, len);
	passert(ret == len, "Write names.txt contents");

	/* lseek to the next file block after names.txt for big_img.jpeg */
	ret = lseek(fd, EZFS_BLOCK_SIZE - len, SEEK_CUR);
	passert(ret >= 0, "Seek to next file block for big_img.jpeg");

	/* big img contents */
	ret = write(fd, pbuf, pret);
	passert(ret == pret, "Write big_img.jpeg contents");

	/* lseek to the next file block after big_img.jpeg for big_txt.txt*/
	ret = lseek(fd, EZFS_BLOCK_SIZE * IMG_BLK - pret, SEEK_CUR);
	passert(ret >= 0, "Seek to next file block for big_txt.txt");

	/* big txt contents */
	ret = write(fd, bbuf, bret);
	passert(ret == bret, "Write big_txt.txt contents");

	/* lseek to the next file block after big_txt.jpeg for big_img.txt
	 * indirect block
	 */
	ret = lseek(fd, EZFS_BLOCK_SIZE * 2 - bret, SEEK_CUR);
	passert(ret >= 0, "Seek to next file block for big_img.txt indirect");

	for (i = 0; i < IMG_BLK - 1; i++)
		indirect_contents[i] = i + 7;

	/* big img indirect contents */
	len = (IMG_BLK - 1) * sizeof(uint64_t);
	ret = write(fd, indirect_contents, len);
	passert(ret == len, "Write big_img.txt indirect contents");

	/* zero unused big_img.jpeg indirect block entries */
	ret = write(fd, zeroes, EZFS_BLOCK_SIZE - len);
	passert(ret == EZFS_BLOCK_SIZE - len, "zero unused big_img.jpeg entries");

	/* big txt indirect contents */
	indirect_contents[0] = 15;
	len = sizeof(uint64_t);
	ret = write(fd, indirect_contents, len);
	passert(ret == len, "Write big_txt.txt indirect contents");

	/* zero unused big_txt.jpeg indirect block entries */
	ret = write(fd, zeroes, EZFS_BLOCK_SIZE - len);
	passert(ret == EZFS_BLOCK_SIZE - len, "zero unused big_txt.jpeg entries");

	ret = fsync(fd);
	passert(ret == 0, "Flush writes to disk");

	close(fd);
	printf("Device [%s] formatted successfully.\n", argv[1]);

	return 0;
}
