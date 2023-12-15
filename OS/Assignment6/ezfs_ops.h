#ifndef __EZFS_OPS_H__
#define __EZFS_OPS_H__

#include "ezfs.h"
#define EZFS_INODES_START_BLOCK 1
#define MYEZFS_MAX_ENTRIES_PER_BLOCK (EZFS_BLOCK_SIZE / sizeof(struct ezfs_dir_entry))

struct dentry *myezfs_lookup(struct inode *parent_inode, struct dentry *child_dentry, unsigned int flags);
static int myezfs_iterate_shared(struct file *filp, struct dir_context *ctx);
static struct inode *ezfs_iget(struct super_block *sb, uint64_t inode_no);
static int ezfs_read_folio(struct file *file, struct folio *folio);
static int ezfs_get_block(struct inode *inode, sector_t block,struct buffer_head *bh_result, int create);
static int ezfs_writepage(struct page *page, struct writeback_control *wbc);
static void ezfs_write_failed(struct address_space *mapping, loff_t to);
static int ezfs_write_begin(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len,
			struct page **pagep, void **fsdata);
static sector_t ezfs_bmap(struct address_space *mapping, sector_t block);
static int ezfs_write_inode(struct inode *inode, struct writeback_control *wbc);
static int ezfs_create(struct user_namespace *mnt_userns, struct inode *dir,
			struct dentry *dentry, umode_t mode, bool excl);
static void ezfs_evict_inode(struct inode *inode);
static int ezfs_unlink(struct inode *dir, struct dentry *dentry);
static int ezfs_link(struct dentry *old, struct inode *dir,
						struct dentry *new);
int ezfs_write_end(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len, unsigned copied,
			struct page *page, void *fsdata);
static int ezfs_mkdir(struct user_namespace *mnt_userns, struct inode *dir,
		       struct dentry *dentry, umode_t mode);

static int ezfs_rmdir(struct inode * dir, struct dentry *dentry);

const struct file_operations ezfs_dir_operations = {

	// .read		= generic_read_dir,
	.iterate_shared	= myezfs_iterate_shared,
	.fsync		= generic_file_fsync,
	.llseek		= generic_file_llseek,
};

const struct inode_operations ezfs_dir_inops = {
	.create				= ezfs_create,
	.lookup				= myezfs_lookup,
	.link				= ezfs_link,
	.unlink				= ezfs_unlink,
	.mkdir 				= ezfs_mkdir, 
	.rmdir				= ezfs_rmdir,
	// .rename			= bfs_rename,
};

const struct file_operations ezfs_file_operations = {
	.llseek 	= generic_file_llseek,
	.read_iter	= generic_file_read_iter,
	.write_iter	= generic_file_write_iter,
	.mmap		= generic_file_mmap,
	.fsync		= generic_file_fsync,
};

const struct address_space_operations ezfs_aops = {
	.dirty_folio	= block_dirty_folio,
	.invalidate_folio = block_invalidate_folio,
	.read_folio	= ezfs_read_folio,
	.writepage	= ezfs_writepage,
	.write_begin	= ezfs_write_begin,
	.write_end	= ezfs_write_end,
	.bmap		= ezfs_bmap,
};

static const struct super_operations ezfs_sops = {
	// .alloc_inode	= bfs_alloc_inode,
	// .free_inode	= bfs_free_inode,
	.write_inode	= ezfs_write_inode,
	.evict_inode	= ezfs_evict_inode,
	// .put_super	= bfs_put_super,
	// .statfs		= bfs_statfs,
};

// extern const struct inode_operations ezfs_inode_ops;
// extern const struct file_operations ezfs_file_ops;

#endif /* ifndef __EZFS_OPS_H__ */
