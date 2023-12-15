#ifndef __EZFS_H__
#define __EZFS_H__

/* An inode contains metadata about the file it represents. This includes
 * permissions, access times, size, etc. All the stuff you can see with the ls
 * command is taken right from the inode.
 *
 * Note that the inode does not contain the file data itself. But it must
 * contain information to find the file data. In our case, we store the block
 * number where the data is.
 */
struct ezfs_inode {
	/* What kind of file this is (i.e. directory, plain old file, etc). */
	mode_t mode;

	uid_t uid;
	gid_t gid;

	struct timespec64 i_atime; /* Access time */
	struct timespec64 i_mtime; /* Modified time */
	struct timespec64 i_ctime; /* Change time */
	unsigned int nlink;

	/* The direct block */
	uint64_t direct_blk_n;
	/* The indirect block */
	uint64_t indirect_blk_n;

	/* A file can be a directory or a plain file. In the latter case
	 * we store the file size. Each directory's size is 4096.
	 */
	uint64_t file_size;

	uint64_t nblocks; /* number of blocks */
};

/* Directories store a mapping from filename -> inode number. Each of these
 * mappings is a single "directory entry" and is represented by the struct
 * below.
 */
#define EZFS_FILENAME_BUF_SIZE (128 - 8 - 1)
#define EZFS_MAX_FILENAME_LENGTH (EZFS_FILENAME_BUF_SIZE - 1)
struct ezfs_dir_entry {
	uint64_t inode_no;
	uint8_t active; /* 0 or 1 */
	char filename[EZFS_FILENAME_BUF_SIZE];
};

/* Macros to set, test, and clear a bit array of integers. */
#define SETBIT(A, k)     (A[((k) / 32)] |=  (1 << ((k) % 32)))
#define CLEARBIT(A, k)   (A[((k) / 32)] &= ~(1 << ((k) % 32)))
#define IS_SET(A, k)     (A[((k) / 32)] &   (1 << ((k) % 32)))

/* This macro will declare a bit vector. I use it to declare an array of the
 * right size inside the ezfs_sb.
 */
#define DECLARE_BIT_VECTOR(name, size) uint32_t name[(size / 32) + 1];

#define EZFS_MAGIC_NUMBER  0x00004118
#define EZFS_BLOCK_SIZE 4096


/* Inode numbers start from 1. It's because if a function is supposed to
 * return an inode number and there's an error, the function returns 0!
 */
#define EZFS_ROOT_INODE_NUMBER 1

/*  Data block #  |  Contents
 * -------------------------------
 *	0         |  Superblock
 *	1         |  Inode Store
 *	2         |  Root Data Block
 */
#define EZFS_SUPERBLOCK_DATABLOCK_NUMBER 0
#define EZFS_INODE_STORE_DATABLOCK_NUMBER 1
#define EZFS_ROOT_DATABLOCK_NUMBER 2

/* The inode store is one 4096 byte-block. The following macro calculates
 * how many ezfs_inodes we can shove in the inode store.
 */
#define EZFS_MAX_INODES (EZFS_BLOCK_SIZE / sizeof(struct ezfs_inode))
#define EZFS_MAX_DATA_BLKS EZFS_MAX_INODES * 512
#define EZFS_MAX_CHILDREN ((loff_t) (EZFS_BLOCK_SIZE / sizeof(struct ezfs_dir_entry)))

#define EZFS_SB_MEMBERS uint64_t version;\
	uint64_t magic;\
	uint64_t disk_blks;\
	DECLARE_BIT_VECTOR(free_inodes, EZFS_MAX_INODES);\
	DECLARE_BIT_VECTOR(free_data_blocks, EZFS_MAX_DATA_BLKS);\
	struct mutex *ezfs_lock;

/* This is the superblock, as it will be serialized onto the disk. */
struct ezfs_super_block {
	EZFS_SB_MEMBERS

	/* Padding, so that this structure takes up the entire block. */
	char __padding__[EZFS_BLOCK_SIZE - sizeof(struct {EZFS_SB_MEMBERS})];
};

/* In the VFS superblock, we need to have a pointer to the buffer_heads for the
 * inode store and superblock so that we can mark them as dirty when they're
 * modified inode.
 */
struct ezfs_sb_buffer_heads {
	struct buffer_head *sb_bh;
	struct buffer_head *i_store_bh;
};
#endif /* ifndef __EZFS_H__ */
