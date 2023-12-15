#include <linux/module.h>
#include <linux/printk.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/fs_context.h>
#include <linux/buffer_head.h>
#include <linux/slab.h>
#include <linux/writeback.h>

// #include "ezfs.h"
#include "ezfs_ops.h"


MODULE_DESCRIPTION("EZFS Kernel Module");
MODULE_AUTHOR("cs4118-group40");
MODULE_LICENSE("GPL");

static struct inode *ezfs_iget(struct super_block *sb, uint64_t inode_no)
{
	struct inode *inode;
	struct ezfs_inode *inode_store;
	struct ezfs_sb_buffer_heads *bh_info;

	if (inode_no > EZFS_MAX_INODES) {
		printk(KERN_INFO "Error: Inode no is too big");
		return NULL;
	}
	bh_info = (struct ezfs_sb_buffer_heads *)sb->s_fs_info;
	inode_store = (struct ezfs_inode *)bh_info->i_store_bh->b_data;

	inode = iget_locked(sb, inode_no);

	if (!(inode->i_state & I_NEW))
		return inode;

	inode->i_mode = inode_store[inode_no - 1].mode;
	inode->i_atime = inode_store[inode_no - 1].i_atime;
	inode->i_mtime = inode_store[inode_no - 1].i_mtime;
	inode->i_ctime = inode_store[inode_no - 1].i_ctime;
	inode->i_size = inode_store[inode_no - 1].file_size;

	inode->i_sb = sb;
	inode->i_ino = inode_no;
	inode->i_blocks = inode_store[inode_no - 1].nblocks;
	inode->i_private = &inode_store[inode_no - 1];
	inode->i_mapping->a_ops = &ezfs_aops;

	if (S_ISDIR(inode->i_mode)) {
	inode->i_fop = &ezfs_dir_operations;
	inode->i_op = &ezfs_dir_inops;
	} else {
		inode->i_fop = &ezfs_file_operations;
		inode->i_op = &ezfs_dir_inops;
	}

	unlock_new_inode(inode);
	return inode;
}

static int myezfs_iterate_shared(struct file *filp, struct dir_context *ctx)
{
	struct inode *inode = file_inode(filp);
	struct ezfs_inode *ezfs_inode = inode->i_private;
	struct buffer_head *bh;
	struct ezfs_dir_entry *dentry;
	uint64_t block;
	int i;
	struct ezfs_inode *inode_store;
	struct ezfs_sb_buffer_heads *bh_info;
	struct super_block *sb = inode->i_sb;

	bh_info = (struct ezfs_sb_buffer_heads *)sb->s_fs_info;
	inode_store = (struct ezfs_inode *)bh_info->i_store_bh->b_data;

	if (!ezfs_inode) {
		return -EFAULT;
	}

	// Emit '.' and '..' entries at the beginning

	if (ctx->pos == 0) {
		if (!dir_emit_dot(filp, ctx)) {
			return 0;
		}
		ctx->pos++;
	}

	if (ctx->pos == 1) {
		if (!dir_emit_dotdot(filp, ctx)) {
			return 0;
		}
		ctx->pos++;
	}

	// read the dentry block from disk
	block = ezfs_inode->direct_blk_n;
	bh = sb_bread(inode->i_sb, block);
	if (!bh) {
		return -EIO;
	}

	dentry = (struct ezfs_dir_entry *)bh->b_data;

	// Iterate over the directory entries, starting from the position after '.' and '..'
	for (i = ctx->pos - 2; i < ezfs_inode->file_size / sizeof(struct ezfs_dir_entry); ++i) {
		if (dentry[i].active) {
			struct inode *dentry_inode = ezfs_iget(sb, dentry[i].inode_no); /* using ezfs_iget correct here ?*/
			if (!dir_emit(ctx, dentry[i].filename, strnlen(dentry[i].filename, EZFS_FILENAME_BUF_SIZE), dentry[i].inode_no, S_DT(dentry_inode->i_mode))) {
				brelse(bh);
				return 0;
			}

			iput(dentry_inode);
		}
		ctx->pos++;
	}
	brelse(bh);
	return 0;
}

uint64_t get_free_disk(struct ezfs_super_block *disk_sb)
{
	uint64_t nblocks = disk_sb->disk_blks;


	for (uint64_t block = 0; block < nblocks; block++) {
		if (!IS_SET(disk_sb->free_data_blocks, block)) {
			SETBIT(disk_sb->free_data_blocks, block);
			return block;
		}
	}
	return 0;
}


uint64_t get_free_disk_inode(struct ezfs_super_block *disk_sb)
{

	for (uint64_t inode = 0; inode < EZFS_MAX_INODES; inode++) {

		if (!IS_SET(disk_sb->free_inodes, inode)) {
			SETBIT(disk_sb->free_inodes, inode);
			return inode;
		}
	}
	return 0;
}

static int ezfs_get_block(struct inode *inode, sector_t block,
			struct buffer_head *bh_result, int create)
{
	unsigned long phys;
	struct super_block *sb = inode->i_sb;
	struct ezfs_sb_buffer_heads *bh_info;
	struct ezfs_super_block *disk_sb;
	uint64_t *table;
	struct buffer_head *bh;
	struct ezfs_inode *ezfs_inode = (struct ezfs_inode *)inode->i_private;
	uint64_t free_block_idx;

	bh_info = sb->s_fs_info;

	disk_sb = (struct ezfs_super_block *)bh_info->sb_bh->b_data;

	printk(KERN_INFO "Value: %llu", (unsigned long long)ezfs_inode->direct_blk_n);

	if (ezfs_inode->direct_blk_n == 0) {
		free_block_idx = get_free_disk(disk_sb);
		if (free_block_idx == 0)
			return -ENOSPC;
		ezfs_inode->direct_blk_n = free_block_idx;
		printk(KERN_INFO "Assigning a direct block: %lld", free_block_idx);
	}

	if (block == 0) {
		phys = ezfs_inode->direct_blk_n;

		if (!create) {
			printk(KERN_INFO "GOING TO READ");
			goto read;
		} else {
			printk(KERN_INFO "GOING TO WRITE");
			goto write;
		}
	}

	if (ezfs_inode->indirect_blk_n != 0) {
		bh = sb_bread(sb, ezfs_inode->indirect_blk_n);
		table = (uint64_t *)(bh->b_data);
		brelse(bh);

		if (block > 512 || block < 0)
			return -ENOSPC;

		phys = table[block - 1];

		if (create)
			goto write;
		if (phys == 0)
			goto out;
		goto read;
	}
	if (ezfs_inode->indirect_blk_n == 0 && create) {
		free_block_idx = get_free_disk(disk_sb);
		if (free_block_idx == 0)
			return -ENOSPC;
		ezfs_inode->indirect_blk_n = free_block_idx;
		bh = sb_bread(sb, ezfs_inode->indirect_blk_n);
		table = (uint64_t *)(bh->b_data);
		brelse(bh);
		goto write;
	}
	read:
		if (!create) {
			if (phys <= disk_sb->disk_blks) {
				map_bh(bh_result, sb, phys);
			}
			// brelse(bh);
			return 0;
		}
	write:

		mutex_lock(disk_sb->ezfs_lock);

		if (phys == 0) {
			free_block_idx = get_free_disk(disk_sb);
			if (free_block_idx == 0)
				return -ENOSPC;
			table[block - 1] = free_block_idx;
			phys = table[block - 1];

			inode->i_blocks++;

			map_bh(bh_result, sb, phys);
			bh = sb_bread(sb, free_block_idx);
			mark_inode_dirty(inode);
			mark_buffer_dirty(bh);
		} else {

			if (inode->i_blocks == 0)
				inode->i_blocks++;

			map_bh(bh_result, sb, phys);
			mark_inode_dirty(inode);
		}
	out:
		brelse(bh);
		mutex_unlock(disk_sb->ezfs_lock);

		return 0;
}

void inode_init_owner(struct user_namespace *mnt_userns, struct inode *inode,
			  const struct inode *dir, umode_t mode)
{
	inode_fsuid_set(inode, mnt_userns);
	if (dir && dir->i_mode & S_ISGID) {
		inode->i_gid = dir->i_gid;

		/* Directories are special, and always inherit S_ISGID */
		if (S_ISDIR(mode))
			mode |= S_ISGID;
	} else
		inode_fsgid_set(inode, mnt_userns);
	inode->i_mode = mode;
}

static int ezfs_create(struct user_namespace *mnt_userns, struct inode *dir,
			  struct dentry *dentry, umode_t mode, bool excl)
{

	struct inode *inode;
	struct super_block *s = dir->i_sb;
	struct ezfs_super_block *ezfs_sb = (struct ezfs_super_block *)((struct ezfs_sb_buffer_heads *)s->s_fs_info)->sb_bh->b_data;
	struct ezfs_inode *ez_inode_store = (struct ezfs_inode *)(((struct ezfs_sb_buffer_heads *)s->s_fs_info)->i_store_bh->b_data);
	unsigned long ino;
	struct buffer_head *bh;
	struct ezfs_dir_entry *dentry_store;
	int err = 0;


	/* first task: create the inode */

	inode = new_inode(s);
	if (!inode)
		return -ENOMEM;
	// mutex_lock(&info->bfs_lock);

	mutex_lock(ezfs_sb->ezfs_lock);


	ino = get_free_disk_inode(ezfs_sb);

	if (ino == 0) {
		printk("no free inodes");
		return -ENOSPC;
	}

	inode_init_owner(&init_user_ns, inode, dir, mode);

	inode->i_mtime = inode->i_atime = inode->i_ctime = current_time(inode);
	inode->i_blocks = 0;
	inode->i_op =  &ezfs_dir_inops;
	inode->i_mapping->a_ops = &ezfs_aops;

	/* if it is a directory */
	if (S_ISDIR(mode)) {
		inode->i_fop = &ezfs_dir_operations;
		inc_nlink(dir);
		inc_nlink(inode);

	} else {
		inode->i_fop = &ezfs_file_operations;
	}

	inode->i_ino = ino+1;

	/* writing the inode back to disk */
	ez_inode_store[ino].i_mtime = inode->i_mtime;
	ez_inode_store[ino].i_atime = inode->i_atime;
	ez_inode_store[ino].i_ctime = inode->i_ctime;
	ez_inode_store[ino].nblocks = inode->i_blocks;

	bh = sb_bread(s, 1);

	inode->i_private = &ez_inode_store[ino];

	insert_inode_hash(inode);

	mark_inode_dirty(inode);
	mark_buffer_dirty(bh);

	brelse(bh);

	/* SECTION 2: add the directory entry to the appropriate dentry block (of the parent) */

	bh = sb_bread(s, ((struct ezfs_inode *)dir->i_private)->direct_blk_n);

	dentry_store = (struct ezfs_dir_entry *)bh->b_data;

	for (uint64_t dentry_idx = 0; dentry_idx < EZFS_MAX_CHILDREN; dentry_idx++) {
		if (dentry_store[dentry_idx].inode_no == 0) {
			dentry_store[dentry_idx].inode_no = ino + 1;
			dentry_store[dentry_idx].active = 1;
			strcpy(dentry_store[dentry_idx].filename, dentry->d_name.name);
			err = 1;
			break;
		}
	}

	mark_buffer_dirty(bh);
	brelse(bh);

	if (!err) {
		inode_dec_link_count(inode);
		mutex_unlock(ezfs_sb->ezfs_lock);
		iput(inode);
		return err;
	}


	mutex_unlock(ezfs_sb->ezfs_lock);
	d_instantiate(dentry, inode);

	return 0;
}

static int ezfs_read_folio(struct file *file, struct folio *folio)
{
	return block_read_full_folio(folio, ezfs_get_block);
}

static int ezfs_writepage(struct page *page, struct writeback_control *wbc)
{
	return block_write_full_page(page, ezfs_get_block, wbc);
}

static void ezfs_write_failed(struct address_space *mapping, loff_t to)
{
	struct inode *inode = mapping->host;

	if (to > inode->i_size)
		truncate_pagecache(inode, inode->i_size);
}

static int ezfs_write_begin(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len,
			struct page **pagep, void **fsdata)
{
	int ret;

	ret = block_write_begin(mapping, pos, len, pagep, ezfs_get_block);
	if (unlikely(ret))
		ezfs_write_failed(mapping, pos + len);

	return ret;
}

int ezfs_write_end(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len, unsigned copied,
			struct page *page, void *fsdata)
{
	struct inode *inode = mapping->host;
	loff_t old_size = inode->i_size;
	bool i_size_changed = false;

	copied = block_write_end(file, mapping, pos, len, copied, page, fsdata);

	/*
	 * No need to use i_size_read() here, the i_size cannot change under us
	 * because we hold i_rwsem.
	 *
	 * But it's important to update i_size while still holding page lock:
	 * page writeout could otherwise come in and zero beyond i_size.
	 */
	if (pos + copied > inode->i_size) {
		i_size_write(inode, pos + copied);
		i_size_changed = true;
	}

	unlock_page(page);
	put_page(page);

	if (old_size < pos)
		pagecache_isize_extended(inode, old_size, pos);
	/*
	 * Don't mark the inode dirty under page lock. First, it unnecessarily
	 * makes the holding time of page lock longer. Second, it forces lock
	 * ordering of page lock and transaction start for journaling
	 * filesystems.
	 */
	if (i_size_changed)
		mark_inode_dirty(inode);
	return copied;
}

static sector_t ezfs_bmap(struct address_space *mapping, sector_t block)
{
	return generic_block_bmap(mapping, block, ezfs_get_block);
}

static int ezfs_unlink(struct inode *dir, struct dentry *dentry)
{
	struct inode *inode = d_inode(dentry);
	struct buffer_head *bh;
	struct ezfs_dir_entry *dentry_store;
	struct ezfs_inode *ezfs_dir;
	struct ezfs_super_block *ezfs_sb;
	uint64_t dentry_idx;
	struct ezfs_sb_buffer_heads *ezfs_bh;
	struct super_block *sb = dir->i_sb;

	ezfs_bh = (struct ezfs_sb_buffer_heads *)sb->s_fs_info;
	ezfs_sb = (struct ezfs_super_block *)ezfs_bh->sb_bh->b_data;

	mutex_lock(ezfs_sb->ezfs_lock);

	ezfs_dir = (struct ezfs_inode *)dir->i_private;
	bh = sb_bread(dir->i_sb, ezfs_dir->direct_blk_n);
	dentry_store = (struct ezfs_dir_entry *)bh->b_data;

	for (dentry_idx = 0; dentry_idx < EZFS_MAX_CHILDREN; dentry_idx++) {
		if (dentry_store[dentry_idx].inode_no == inode->i_ino) {
			dentry_store[dentry_idx].active = 0;
			dentry_store[dentry_idx].inode_no = 0;

			/* deinstantiate the file name ?*/
			mark_buffer_dirty_inode(bh, dir);
			dir->i_ctime = dir->i_mtime = current_time(dir);
			mark_inode_dirty(dir);
			inode->i_ctime = dir->i_ctime;
			inode_dec_link_count(inode);
			break;
		}
	}
	brelse(bh);
	mutex_unlock(ezfs_sb->ezfs_lock);
	return (dentry_idx == EZFS_MAX_CHILDREN) ? -ENOENT : 0;
}

static int ezfs_link(struct dentry *old, struct inode *dir,
						struct dentry *new)
{
	return 0;
}

static int ezfs_write_inode(struct inode *inode, struct writeback_control *wbc)
{
	int err = 0;
	struct ezfs_inode *ezfs_inode = (struct ezfs_inode *)inode->i_private;
	struct super_block *sb;
	struct buffer_head *bh;
	struct ezfs_super_block *ezfs_sb;
	struct ezfs_sb_buffer_heads *ezfs_bh;
	// struct ezfs_sb_buffer_heads* ezfs_bh;

	sb = inode->i_sb;
	ezfs_bh = (struct ezfs_sb_buffer_heads *)sb->s_fs_info;
	ezfs_sb = (struct ezfs_super_block *)ezfs_bh->sb_bh->b_data;

	mutex_lock(ezfs_sb->ezfs_lock);

	// ezfs_bh = (struct ezfs_sb_buffer_heads*)sb->s_fs_info;
	bh = sb_bread(sb, EZFS_INODES_START_BLOCK);

	ezfs_inode->mode = inode->i_mode;
	ezfs_inode->i_atime = inode->i_atime;
	ezfs_inode->i_mtime = inode->i_mtime;
	ezfs_inode->i_ctime = inode->i_ctime;
	ezfs_inode->file_size = inode->i_size;
	ezfs_inode->nblocks = inode->i_blocks; /*not sure about this*/
	ezfs_inode->nlink = inode->i_nlink;
	ezfs_inode->uid = cpu_to_le32(i_uid_read(inode));;
	ezfs_inode->gid = cpu_to_le32(i_gid_read(inode));
	mark_buffer_dirty(bh);
	if (wbc->sync_mode == WB_SYNC_ALL) {
		sync_dirty_buffer(bh);
		if (buffer_req(bh) && !buffer_uptodate(bh))
			err = -EIO;
	}
	brelse(bh);
	mutex_unlock(ezfs_sb->ezfs_lock);
	return err;
}

static void ezfs_evict_inode(struct inode *inode)
{
	unsigned long ino = inode->i_ino;
	struct ezfs_inode *ezfs_inode = (struct ezfs_inode *)inode->i_private;
	struct buffer_head *bh;
	struct ezfs_sb_buffer_heads *ezfs_bh;
	struct ezfs_super_block *ezfs_sb;
	uint64_t *table;
	struct super_block *sb = inode->i_sb;
	ezfs_bh = (struct ezfs_sb_buffer_heads *)sb->s_fs_info;
	ezfs_sb = (struct ezfs_super_block *)ezfs_bh->sb_bh->b_data;

	truncate_inode_pages_final(&inode->i_data);
	invalidate_inode_buffers(inode);
	clear_inode(inode);

	if (inode->i_nlink)
		return;

	mutex_lock(ezfs_sb->ezfs_lock);

	/* freeing the inode entry*/
	CLEARBIT(ezfs_sb->free_inodes, ino-1);

	/*freeing the direct block */
	CLEARBIT(ezfs_sb->free_data_blocks, ezfs_inode->direct_blk_n);
	if (ezfs_inode->indirect_blk_n != 0) {
		bh = sb_bread(sb, ezfs_inode->indirect_blk_n);
		table = (uint64_t *)bh->b_data;
		brelse(bh);
		for (uint64_t entry = 0; entry < 512; entry++) {
			if (table[entry] != 0) {
				CLEARBIT(ezfs_sb->free_data_blocks, table[entry]);
			}
		}
		mark_buffer_dirty(bh);
	}

	memset(ezfs_inode, 0, sizeof(struct ezfs_inode));
	bh = sb_bread(sb, EZFS_INODE_STORE_DATABLOCK_NUMBER);
	mark_buffer_dirty(bh);
	brelse(bh);

	mutex_unlock(ezfs_sb->ezfs_lock);


}

static int ezfs_rmdir(struct inode *dir, struct dentry *dentry)
{
	struct inode *inode = d_inode(dentry);
	struct ezfs_inode *ezfs_inode;
	int err = -ENOTEMPTY;

	ezfs_inode = (struct ezfs_inode *)inode->i_private;

	if (ezfs_inode->direct_blk_n == 0) {
		err = ezfs_unlink(dir, dentry);
		if (!err) {
			inode_dec_link_count(dir);
			inode_dec_link_count(inode);
		}
	}

	return err;
}

struct dentry *myezfs_lookup(struct inode *parent_inode, struct dentry *child_dentry, unsigned int flags)
{
	struct ezfs_inode *parent_myezfs_inode = (struct ezfs_inode *)parent_inode->i_private;
	struct buffer_head *bh;
	struct ezfs_dir_entry *entry;
	struct inode *inode = NULL;
	struct super_block *sb = parent_inode->i_sb;
	struct ezfs_sb_buffer_heads *ezfs_bh = (struct ezfs_sb_buffer_heads *)sb->s_fs_info;
	struct ezfs_super_block *disk_sb = (struct ezfs_super_block *)ezfs_bh->sb_bh->b_data;
	int i;

	mutex_lock(disk_sb->ezfs_lock);

	if (!parent_myezfs_inode) {
		mutex_unlock(disk_sb->ezfs_lock);
		return d_splice_alias(NULL, child_dentry);
	}

	// Check if parent inode is a directory
	if (!S_ISDIR(parent_myezfs_inode->mode)) {
		mutex_unlock(disk_sb->ezfs_lock);
		return ERR_PTR(-ENOTDIR);
	}

	// Read the block containing directory entries
	bh = sb_bread(parent_inode->i_sb, parent_myezfs_inode->direct_blk_n);
	if (!bh) {
		mutex_unlock(disk_sb->ezfs_lock);
		return ERR_PTR(-EIO);
	}

	entry = (struct ezfs_dir_entry *)bh->b_data;
	for (i = 0; i < MYEZFS_MAX_ENTRIES_PER_BLOCK; ++i) {
		if (entry[i].active && !memcmp(child_dentry->d_name.name, entry[i].filename, child_dentry->d_name.len)) {
			// Found the file/directory, load its inode
			inode = ezfs_iget(parent_inode->i_sb, entry[i].inode_no);
			if (!inode) {
				brelse(bh);
				mutex_unlock(disk_sb->ezfs_lock);
				return ERR_PTR(-EIO);
			}

			// brelse(bh);
			mutex_unlock(disk_sb->ezfs_lock);
			return d_splice_alias(inode, child_dentry);
		}
	}

	brelse(bh);
	mutex_unlock(disk_sb->ezfs_lock);
	return d_splice_alias(NULL, child_dentry);
}

static int myezfs_fill_super(struct super_block *sb, struct fs_context *fc)
{

		struct ezfs_sb_buffer_heads *bh_info;
		struct ezfs_super_block *disk_sb;
		struct ezfs_inode *disk_inode_store;
		struct inode *root_inode;
		struct buffer_head *sb_buffer_head;
		struct buffer_head *inode_buffer_head;
		int ret = -EINVAL;

		bh_info = kmalloc(sizeof(struct ezfs_sb_buffer_heads), GFP_KERNEL);
		if (!bh_info) {
			return -ENOMEM;
		}

		sb_set_blocksize(sb, EZFS_BLOCK_SIZE);

		/* Read superblock from disk */
		sb_buffer_head  = sb_bread(sb, 0);
		if (!sb_buffer_head) {
			kfree(bh_info);
			return -EIO;
		}

		disk_sb = (struct ezfs_super_block *)(sb_buffer_head->b_data);

		disk_sb->ezfs_lock = kmalloc(sizeof(struct mutex), GFP_KERNEL);
		if (!disk_sb->ezfs_lock) {
			return -ENOMEM;
		}

		mutex_init(disk_sb->ezfs_lock);


		if (disk_sb->magic != EZFS_MAGIC_NUMBER) {
			brelse(sb_buffer_head);
			kfree(bh_info);
			return -EINVAL;
			goto failed_magic_check;
		}

		inode_buffer_head = sb_bread(sb, EZFS_INODES_START_BLOCK);

		if (!inode_buffer_head) {
			ret = -EIO;
			goto failed_inode_read;
		}

		disk_inode_store = (struct ezfs_inode *) (inode_buffer_head->b_data);

		// Store the buffer head information in the VFS superblock
		bh_info->sb_bh = sb_buffer_head;
		bh_info->i_store_bh = inode_buffer_head;
		sb->s_fs_info = bh_info;
		sb->s_magic = EZFS_MAGIC_NUMBER;

		sb->s_op = &ezfs_sops;	  // Set the superblock operations

		// // Create a VFS inode for the root directory
		root_inode = ezfs_iget(sb, EZFS_ROOT_INODE_NUMBER);
		if (!root_inode) {
			ret = -ENOMEM;
			goto failed_inode_alloc;
		}

		// Create the dentry for the root inode
		sb->s_root = d_make_root(root_inode);

		if (!sb->s_root) {
			ret = -ENOMEM;
			goto failed_dentry_creation;
		}

	brelse(inode_buffer_head);
	brelse(sb_buffer_head);

	return 0;

	/* Clean up on failure */
	failed_dentry_creation:
		iput(root_inode);
	failed_inode_alloc:
		brelse(bh_info->sb_bh);
	failed_inode_read:
		brelse(bh_info->sb_bh);
	failed_magic_check:
		brelse(bh_info->i_store_bh);

	return ret;
}

static int myezfs_get_tree(struct fs_context *fc)
{
	return get_tree_bdev(fc, myezfs_fill_super);
}

static const struct fs_context_operations myezfs_context_ops = {
	.get_tree	= myezfs_get_tree,
};


static int ezfs_mkdir(struct user_namespace *mnt_userns, struct inode *dir,
			   struct dentry *dentry, umode_t mode)
{
	ezfs_create(mnt_userns, dir, dentry, mode | S_IFDIR, 0);
	return 0;
}


int myezfs_init_fs_context(struct fs_context *fc)
{
	fc->ops = &myezfs_context_ops;
	return 0;
}

static void ezfs_kill_sb(struct super_block *sb)
{

	struct ezfs_sb_buffer_heads *ezfs_bh;
	struct ezfs_super_block *ezfs_sb;

	ezfs_bh = (struct ezfs_sb_buffer_heads *)sb->s_fs_info;
	ezfs_sb = (struct ezfs_super_block *)ezfs_bh->sb_bh->b_data;

	kfree(ezfs_sb->ezfs_lock);

	kfree(sb->s_fs_info);
	kill_block_super(sb);
}

static struct file_system_type myezfs_fs_type = {
	.owner = THIS_MODULE,
	.name		= "myezfs",
	.init_fs_context = myezfs_init_fs_context,
	.kill_sb	= ezfs_kill_sb,
};


static int __init init_myezfs_fs(void)
{
	return register_filesystem(&myezfs_fs_type);
}

static void __exit exit_myezfs_fs(void)
{
	unregister_filesystem(&myezfs_fs_type);
}

module_init(init_myezfs_fs)
module_exit(exit_myezfs_fs)


