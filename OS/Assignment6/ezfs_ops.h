#ifndef __EZFS_OPS_H__
#define __EZFS_OPS_H__

void ezfs_evict_inode(struct inode *inode);
int ezfs_write_inode(struct inode *inode, struct writeback_control *wbc);

struct super_operations ezfs_sb_ops = {
	.evict_inode = ezfs_evict_inode,
	.write_inode = ezfs_write_inode,
};

struct dentry *ezfs_lookup(struct inode *parent, struct dentry *child_dentry,
			unsigned int flags);
int ezfs_create(struct user_namespace *mnt_userns, struct inode *parent, struct dentry *dentry, umode_t mode, bool excl);
int ezfs_unlink(struct inode *dir, struct dentry *dentry);
int ezfs_mkdir(struct user_namespace *mnt_userns, struct inode *dir, struct dentry *dentry, umode_t mode);
int ezfs_rmdir(struct inode *dir, struct dentry *dentry);
int ezfs_setattr(struct user_namespace *, struct dentry *, struct iattr *);
/* hard links and symlinks not needed for this assignment */

const struct inode_operations ezfs_inode_ops = {
	.lookup = ezfs_lookup,
	.create = ezfs_create,
	.unlink = ezfs_unlink,
	.mkdir = ezfs_mkdir,
	.rmdir = ezfs_rmdir,
	.setattr = ezfs_setattr,
};

int ezfs_iterate(struct file *filp, struct dir_context *ctx);
int ezfs_read_folio(struct file *file, struct folio *folio);
int ezfs_writepage(struct page *page, struct writeback_control *wbc);
int ezfs_write_begin(struct file *file, struct address_space *mapping,
		loff_t pos, unsigned int len,
		struct page **pagep, void **fsdata);
int ezfs_write_end(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len, unsigned copied,
			struct page *page, void *fsdata);

const struct file_operations ezfs_dir_ops = {
	.owner = THIS_MODULE,
	.iterate_shared = ezfs_iterate,
};

const struct file_operations ezfs_file_ops = {
	.owner = THIS_MODULE,
	.llseek = generic_file_llseek,
	.read_iter = generic_file_read_iter,
	.write_iter	= generic_file_write_iter,
	.mmap = generic_file_mmap,
	.splice_read = generic_file_splice_read,
	.fsync = generic_file_fsync,
};

const struct address_space_operations ezfs_aops = {
	.dirty_folio	= block_dirty_folio,
	.invalidate_folio = block_invalidate_folio,
	.read_folio = ezfs_read_folio,
	.writepage = ezfs_writepage,
	.write_begin = ezfs_write_begin,
	.write_end = ezfs_write_end,
};
#endif /* ifndef __EZFS_OPS_H__ */
