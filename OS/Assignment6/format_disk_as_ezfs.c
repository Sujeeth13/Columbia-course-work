/*
 * format_disk_as_ezfs
 *
 * This program formats a disk as a harcoded ezfs filesystem.
 *
 *  <==================================================================>
 * |  ---------------------						|
 * | ( superblock            )						|
 * |   ---------------------						|
 * |   ---------------------						|
 * | ( inode store           )						|
 * |   ---------------------						|
 * |   ---------------------						|
 * | / ROOT (AKA `/`)        \						|
 * | | -> hello.txt          | <-- ROOT_DATA_BLOCK_NUMBER (2)		|
 * | \ -> subdir             /						|
 * |   ---------------------						|
 * |   ---------------------						|
 * | ( /hello.txt            )						|
 * |   ---------------------						|
 * |   ---------------------						|
 * | / /subdir/              \						|
 * | | -> names.txt          |						|
 * | | -> big_img.jpeg       |						|
 * | \ -> big_txt.txt        /						|
 * |   ---------------------						|
 * |   ---------------------						|
 * | ( /subdir/names.txt     )						|
 * |   ---------------------						|
 * |   ---------------------						|
 * | ( /subdir/big_img.jpeg ) <-- data block 6 (2 + 4)			|
 * |  ...								|
 * | ( /subdir/big_img.jpeg  ) <-- data block 13 (2 + 11)		|
 * |   ---------------------						|
 * |   ---------------------						|
 * | ( /subdir/big_txt.txt   ) <-- data block 14 (2 + 12)		|
 * |   ---------------------						|
 * | ( /subdir/big_txt.txt   )						|
 * |   ---------------------						|
 * |   ---------------------						|
 * | / INDIRECT BLOCK        \						|
 * | | for big_img.jpeg      |						|
 * | | -> data block 7       | <-- data block 16 (2 + 14)		|
 * | | ...		     |						|
 * | \ -> data block 13      /						|
 * |   ---------------------						|
 * |   ---------------------						|
 * | / INDIRECT BLOCK        \						|
 * | | for big_txt.txt       | <-- data block 17 (2 + 15)		|
 * | \ -> data block 14      /						|
 * |   ---------------------						|
 * |   ...								|
 *  \__________________________________________________________________/
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdarg.h>

/* These are the same on a 64-bit architecture */
#define timespec64 timespec

#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME 0
#endif
#ifndef S_IFDIR
#define S_IFDIR  0040000
#endif

#include "ezfs.h"

#define PASSERT_BUF_SIZE 1024
char passert_buf[PASSERT_BUF_SIZE];

void passert(int condition, char *message, ...)
{
	va_list args;

	va_start(args, message);
	vsnprintf(passert_buf, PASSERT_BUF_SIZE, message, args);
	va_end(args);

	printf("[%s] %s\n", condition ? " OK " : "FAIL", passert_buf);
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

// FS layout (hard-coded):
/*
 * 1.  /
 * 2.  ├── hello.txt
 * 3.  └── subdir
 * 5.      ├── names.txt
 * 6.      ├── big_img.jpeg
 * 7.      └── big_txt.txt
 */
#define ROOT_INODE_NUMBER	1 /* also defined as EZFS_ROOT_INODE_NUMBER */
#define HELLO_TXT_INODE_NUMBER	2
#define SUBDIR_INODE_NUMBER	3
#define NAMES_TXT_INODE_NUMBER	4
#define BIG_IMG_INODE_NUMBER	5
#define BIG_TXT_INODE_NUMBER	6
// thus,
#define NUM_INODES		6

#define ROOT_NUM_CHILDREN	2
#define SUBDIR_NUM_CHILDREN	3

// the following define's are offsets from the block that contains the root of
// the filesystem (i.e. the first block after the inode store)
/** offset from root (AKA '/') data block to the block containing the contents of hello.txt */
#define HELLO_TXT_OFFSET	1
/** offset from root data block to the block containing the `/subdir` directory
 * entry */
#define SUBDIR_OFFSET		2
/** offset from root data block to the block containing the contents of `/subdir/names.txt` */
#define NAMES_TXT_OFFSET	3
/** offset from root data block to the "direct block" containing the first block
 * of the contents of `/subdir/big_img.jpeg` */
#define BIG_IMG_DIRECT_OFFSET	4
/** offset from root data block to the "indirect block" containing the a
 * reference block that points to the blocks containing the rest of
 * `/subdir/big_img.jpeg` */
#define BIG_IMG_INDIRECT_OFFSET	14
/** offset from root data block to the "direct block" containing the first block
 * of the contents of `/subdir/big_txt.txt` */
#define BIG_TXT_DIRECT_OFFSET	12
/** offset from root data block to the "indirect block" containing a
 * reference block that points to the blocks containing the rest of
 * `/subdir/big_txt.txt` */
#define BIG_TXT_INDIRECT_OFFSET	15
// see the diagram in the file header for the layout

char *big_img_path = "./big_files/big_img.jpeg";
char *big_txt_path = "./big_files/big_txt.txt";

int main(int argc, char *argv[])
{
	/** File Descriptor for the device */
	int fd;
	/** number of blocks on the device */
	int disk_blks;
	ssize_t ret, len;
	/** SuperBlock */
	struct ezfs_super_block sb;
	struct ezfs_inode inode;
	struct ezfs_dir_entry dentry;
	uint64_t indirect_block[EZFS_BLOCK_SIZE / sizeof(uint64_t)];

	char *hello_contents = "Hello world!\n";
	char *names_contents = "Noam Bechhofer\n"
			       "Sujeeth Bhavanam\n"
			       "Nicholas Ching\n";
	char buf[EZFS_BLOCK_SIZE];
	const char zeroes[EZFS_BLOCK_SIZE] = { 0 };

	FILE *big_img;
	FILE *big_txt;

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

	sb.version = 1;
	sb.magic = EZFS_MAGIC_NUMBER;
	sb.disk_blks = disk_blks;

	SETBIT(sb.free_inodes, 0); // `/`
	SETBIT(sb.free_inodes, 1); // `/hello.txt`
	SETBIT(sb.free_inodes, 2); // `/subdir`
	SETBIT(sb.free_inodes, 3); // `/subdir/names.txt`
	SETBIT(sb.free_inodes, 4); // `/subdir/big_img.jpeg`
	SETBIT(sb.free_inodes, 5); // `/subdir/big_txt.txt

	for (int i = 0; i < 20; i++) {
		SETBIT(sb.free_data_blocks, i);
	}

	/* Write the superblock to the first block of the filesystem. */
	ret = write(fd, (char *)&sb, sizeof(sb));
	passert(ret == EZFS_BLOCK_SIZE, "Write superblock");
	/* since we wrote an entire block, we have advanced to the second block,
	 * the inode store */

	/*
	 * write the inode store
	 */

	/* The root directory will take the first inode. */
	passert(lseek(fd, 0, SEEK_CUR) == EZFS_BLOCK_SIZE + (ROOT_INODE_NUMBER - 1) * sizeof(struct ezfs_inode),
			"In correct position for root inode");
	inode_reset(&inode);
	inode.mode = S_IFDIR | 0777;
	inode.nlink = 3; // /mnt/ez (in the host FS), `/.`, and `/subdir/..`
	inode.direct_blk_n = EZFS_ROOT_DATABLOCK_NUMBER;
	inode.file_size = EZFS_BLOCK_SIZE;
	inode.nblocks = 1;

	/* Write the root inode */
	ret = write(fd, (char *)&inode, sizeof(inode));
	passert(ret == sizeof(inode), "Write root inode");

	/* The hello.txt file will take inode num following root inode num. */
	passert(lseek(fd, 0, SEEK_CUR) == EZFS_BLOCK_SIZE + (HELLO_TXT_INODE_NUMBER - 1) * sizeof(struct ezfs_inode),
			"In correct position for hello.txt inode");
	inode_reset(&inode);
	inode.nlink = 1;
	inode.mode = S_IFREG | 0666;
	inode.direct_blk_n = EZFS_ROOT_DATABLOCK_NUMBER + HELLO_TXT_OFFSET;
	inode.file_size = strlen(hello_contents);
	inode.nblocks = 1;

	/* Write the hello.txt inode after the root inode. */
	ret = write(fd, (char *) &inode, sizeof(inode));
	passert(ret == sizeof(inode), "Write hello.txt inode");

	/* The subdir directory will take inode num following hello.txt inode num. */
	passert(lseek(fd, 0, SEEK_CUR) == EZFS_BLOCK_SIZE + (SUBDIR_INODE_NUMBER - 1) * sizeof(struct ezfs_inode),
			"In correct position for subdir inode");
	inode_reset(&inode);
	inode.mode = S_IFDIR | 0777;
	inode.nlink = 2; // `/subdir` and `/subdir/.`
	inode.direct_blk_n = EZFS_ROOT_DATABLOCK_NUMBER + SUBDIR_OFFSET;
	inode.file_size = EZFS_BLOCK_SIZE;
	inode.nblocks = 1;

	/* Write the subdir inode continuing after the hello.txt inode. */
	ret = write(fd, (char *) &inode, sizeof(inode));
	passert(ret == sizeof(inode), "Write subdir inode");

	/* The names.txt file will take inode num following subdir inode num. */
	passert(lseek(fd, 0, SEEK_CUR) == EZFS_BLOCK_SIZE + (NAMES_TXT_INODE_NUMBER - 1) * sizeof(struct ezfs_inode),
			"In correct position for names.txt inode");
	inode_reset(&inode);
	inode.nlink = 1;
	inode.mode = S_IFREG | 0666;
	inode.direct_blk_n = EZFS_ROOT_DATABLOCK_NUMBER + NAMES_TXT_OFFSET;
	inode.file_size = strlen(names_contents);
	inode.nblocks = 1;

	/* Write the names.txt inode after the subdir inode. */
	ret = write(fd, (char *) &inode, sizeof(inode));
	passert(ret == sizeof(inode), "Write names.txt inode");

	/* The big_img.jpeg file will take inode num following names.txt inode num. */
	passert(lseek(fd, 0, SEEK_CUR) == EZFS_BLOCK_SIZE + (BIG_IMG_INODE_NUMBER - 1) * sizeof(struct ezfs_inode),
			"In correct position for big_img.jpeg inode");
	inode_reset(&inode);
	inode.nlink = 1;
	inode.mode = S_IFREG | 0666;
	inode.direct_blk_n = EZFS_ROOT_DATABLOCK_NUMBER + BIG_IMG_DIRECT_OFFSET;
	inode.indirect_blk_n = EZFS_ROOT_DATABLOCK_NUMBER + BIG_IMG_INDIRECT_OFFSET;
	inode.file_size = 29296;
	inode.nblocks = 8;

	/* Write the big_img.jpeg inode after the names.txt inode. */
	ret = write(fd, (char *) &inode, sizeof(inode));
	passert(ret == sizeof(inode), "Write big_img.jpeg inode");

	/* The big_txt.txt file will take inode num following big_img.jpeg inode num. */
	passert(lseek(fd, 0, SEEK_CUR) == EZFS_BLOCK_SIZE + (BIG_TXT_INODE_NUMBER - 1) * sizeof(struct ezfs_inode),
			"In correct position for big_txt.txt inode");
	inode_reset(&inode);
	inode.nlink = 1;
	inode.mode = S_IFREG | 0666;
	inode.direct_blk_n = EZFS_ROOT_DATABLOCK_NUMBER + BIG_TXT_DIRECT_OFFSET;
	inode.indirect_blk_n = EZFS_ROOT_DATABLOCK_NUMBER + BIG_TXT_INDIRECT_OFFSET;
	inode.file_size = 4169;
	inode.nblocks = 2;

	/* Write the big_txt.txt inode after the big_img.jpeg inode. */
	ret = write(fd, (char *) &inode, sizeof(inode));
	passert(ret == sizeof(inode), "Write big_txt.txt inode");

	/* lseek to the third data block, the root data block */
	ret = lseek(fd, EZFS_BLOCK_SIZE - NUM_INODES * sizeof(struct ezfs_inode),
		SEEK_CUR);
	passert(ret >= 0 && lseek(fd, 0, SEEK_CUR) % EZFS_BLOCK_SIZE == 0,
			"Seek past inode table");

	/* root dentries */

	/* dentry for hello.txt */
	dentry_reset(&dentry);
	strncpy(dentry.filename, "hello.txt", sizeof(dentry.filename));
	dentry.active = 1;
	dentry.inode_no = HELLO_TXT_INODE_NUMBER;

	ret = write(fd, (char *) &dentry, sizeof(dentry));
	passert(ret == sizeof(dentry), "Write dentry for hello.txt");

	/* dentry for subdir */
	dentry_reset(&dentry);
	strncpy(dentry.filename, "subdir", sizeof(dentry.filename));
	dentry.active = 1;
	dentry.inode_no = SUBDIR_INODE_NUMBER;

	ret = write(fd, (char *) &dentry, sizeof(dentry));
	passert(ret == sizeof(dentry), "Write dentry for subdir");

	/* pad 0's to the next data block */
	len = EZFS_BLOCK_SIZE - ROOT_NUM_CHILDREN * sizeof(struct ezfs_dir_entry);
	ret = write(fd, zeroes, len);
	passert(ret == len && lseek(fd, 0, SEEK_CUR) % EZFS_BLOCK_SIZE == 0,
			"Pad 0's to end of root dentries");

	/* hello.txt contents */
	len = strlen(hello_contents);
	strncpy(buf, hello_contents, len);
	ret = write(fd, buf, len);
	passert(ret == len, "Write hello.txt contents");

	/* pad 0's to the next data block */
	ret = write(fd, zeroes, EZFS_BLOCK_SIZE - len);
	passert(ret == EZFS_BLOCK_SIZE - len && lseek(fd, 0, SEEK_CUR) % EZFS_BLOCK_SIZE == 0,
			"Pad 0's to end of hello.txt contents");

	/*
	 * subdir dentries:
	 */

	/* dentry for names.txt */
	dentry_reset(&dentry);
	strncpy(dentry.filename, "names.txt", sizeof(dentry.filename));
	dentry.active = 1;
	dentry.inode_no = NAMES_TXT_INODE_NUMBER;

	ret = write(fd, (char *) &dentry, sizeof(dentry));
	passert(ret == sizeof(dentry), "Write dentry for names.txt");


	/* dentry for big_img.jpeg */
	dentry_reset(&dentry);
	strncpy(dentry.filename, "big_img.jpeg", sizeof(dentry.filename));
	dentry.active = 1;
	dentry.inode_no = BIG_IMG_INODE_NUMBER;

	ret = write(fd, (char *) &dentry, sizeof(dentry));
	passert(ret == sizeof(dentry), "Write dentry for big_img.jpeg");

	/* dentry for big_txt.txt */
	dentry_reset(&dentry);
	strncpy(dentry.filename, "big_txt.txt", sizeof(dentry.filename));
	dentry.active = 1;
	dentry.inode_no = BIG_TXT_INODE_NUMBER;

	ret = write(fd, (char *) &dentry, sizeof(dentry));
	passert(ret == sizeof(dentry), "Write dentry for big_txt.txt");

	/* pad 0's to the next data block */
	len = EZFS_BLOCK_SIZE - SUBDIR_NUM_CHILDREN * sizeof(struct ezfs_dir_entry);
	ret = write(fd, zeroes, len);
	passert(ret == len && lseek(fd, 0, SEEK_CUR) % EZFS_BLOCK_SIZE == 0,
			"Pad 0's to end of subdir dentries");

	passert(lseek(fd, 0, SEEK_CUR) == 0x5000,
			"in correct data block for names.txt contents");

	/* names.txt contents */
	len = strlen(names_contents);
	strncpy(buf, names_contents, len);
	ret = write(fd, buf, len);
	passert(ret == len, "Write names.txt contents");

	/* pad 0's to the next data block */
	ret = write(fd, zeroes, EZFS_BLOCK_SIZE - len);
	passert(ret == EZFS_BLOCK_SIZE - len && lseek(fd, 0, SEEK_CUR) % EZFS_BLOCK_SIZE == 0,
			"Pad 0's to end of names.txt contents");

	passert(lseek(fd, 0, SEEK_CUR) == 0x6000,
			"in correct data block for big_img.jpeg contents");

	/* big_img.jpeg contents */
	big_img = fopen(big_img_path, "r");
	passert(big_img != NULL, "Open big_img.jpeg");

	for (int i = 0; !feof(big_img); i++) {
		len = fread(buf, 1, EZFS_BLOCK_SIZE, big_img);
		ret = write(fd, buf, len);
		sprintf(buf, "Write big_img.jpeg contents block %d", i);
		passert(ret == len, buf);
	}
	ret = fclose(big_img);
	passert(ret == 0, "Close big_img.jpeg");

	/* pad 0's to the next data block */
	ret = write(fd, zeroes, EZFS_BLOCK_SIZE - len);
	passert(ret == EZFS_BLOCK_SIZE - len && lseek(fd, 0, SEEK_CUR) % EZFS_BLOCK_SIZE == 0,
			"Pad 0's to end of big_img.jpeg contents");

	passert(lseek(fd, 0, SEEK_CUR) == 0xe000,
			"in correct data block for big_txt.txt contents");

	/* big_txt.txt contents */
	big_txt = fopen(big_txt_path, "r");
	passert(big_txt != NULL, "Open big_txt.txt");

	for (int i = 0; !feof(big_txt); i++) {
		len = fread(buf, 1, EZFS_BLOCK_SIZE, big_txt);
		ret = write(fd, buf, len);
		sprintf(buf, "Write big_txt.txt contents block %d", i);
		passert(ret == len, buf);
	}
	ret = fclose(big_txt);
	passert(ret == 0, "Close big_txt.txt");

	/* pad 0's to the next data block */
	ret = write(fd, zeroes, EZFS_BLOCK_SIZE - len);
	passert(ret == EZFS_BLOCK_SIZE - len && lseek(fd, 0, SEEK_CUR) % EZFS_BLOCK_SIZE == 0,
			"Pad 0's to end of big_txt.txt contents");

	/* indirect block for big_img.jpeg */
	for (int i = 1; i < 8; i++) { // 8 blocks, 1 already written to direct block
		indirect_block[i - 1] = EZFS_ROOT_DATABLOCK_NUMBER + BIG_IMG_DIRECT_OFFSET + i;
	}
	ret = write(fd, (char *) &indirect_block, 7 * sizeof(uint64_t));
	passert(ret == 7 * sizeof(uint64_t), "Write indirect block pointers for big_img.jpeg");

	/*
	 * Note how this works. We have 7 offsets in the indirect block which
	 * point to the data blocks in which this rest of this file (after its
	 * direct block) is stored. We will know that the seventh block is
	 * the last because the rest of the indirect block is zeroed out.
	 */
	len = EZFS_BLOCK_SIZE - 7 * sizeof(uint64_t);
	ret = write(fd, zeroes, len);
	passert(ret == len, "Pad 0's to end of indirect block for big_img.jpeg");

	/* indirect block for big_txt.txt */
	indirect_block[0] = EZFS_ROOT_DATABLOCK_NUMBER + BIG_TXT_DIRECT_OFFSET + 1;
	ret = write(fd, (char *) &indirect_block, sizeof(uint64_t));
	passert(ret == sizeof(uint64_t), "Write indirect block pointers for big_txt.txt");

	/* pad 0's to the next data block */
	ret = write(fd, zeroes, EZFS_BLOCK_SIZE - sizeof(uint64_t));
	passert(ret == EZFS_BLOCK_SIZE - sizeof(uint64_t) && lseek(fd, 0, SEEK_CUR) % EZFS_BLOCK_SIZE == 0,
			"Pad 0's to end of indirect block for big_txt.txt");

	ret = fsync(fd);
	passert(ret == 0, "Flush writes to disk");

	close(fd);
	printf("Device [%s] formatted successfully.\n", argv[1]);

	return 0;
}
