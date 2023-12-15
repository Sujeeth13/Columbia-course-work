#include <linux/blkdev.h>
#include <linux/buffer_head.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/writeback.h>
#include <linux/fs_context.h>
#include <linux/pagemap.h>

#include "ezfs.h"
#include "ezfs_ops.h"

#define MIN(a, b) ((a)<(b) ? (a):(b))
#define ezfs_spc(sb) MIN(sb->disk_blks - 2, EZFS_MAX_DATA_BLKS)

static inline struct buffer_head *get_ezfs_sb_bh(struct super_block *sb)
{
	return ((struct ezfs_sb_buffer_heads *)sb->s_fs_info)->sb_bh;
}

static inline struct ezfs_super_block *get_ezfs_sb(struct super_block *sb)
{
	return (struct ezfs_super_block *) get_ezfs_sb_bh(sb)->b_data;
}

static inline struct buffer_head *get_ezfs_i_bh(struct super_block *sb)
{
	return ((struct ezfs_sb_buffer_heads *)sb->s_fs_info)->i_store_bh;
}

static inline struct ezfs_inode *get_ezfs_inode(struct inode *inode)
{
	return inode->i_private;
}

static struct inode *ezfs_iget(struct super_block *sb, int ino)
{
	struct inode *inode = iget_locked(sb, ino);

	if (inode && inode->i_state & I_NEW) {
		struct ezfs_inode *ezfs_inode = (struct ezfs_inode *)
			get_ezfs_i_bh(sb)->b_data + ino -
			EZFS_ROOT_INODE_NUMBER;

		inode->i_private = ezfs_inode;
		inode->i_mode = ezfs_inode->mode;
		inode->i_op = &ezfs_inode_ops;
		inode->i_sb = sb;
		if (inode->i_mode & S_IFDIR)
			inode->i_fop = &ezfs_dir_ops;
		else
			inode->i_fop = &ezfs_file_ops;
		inode->i_mapping->a_ops = &ezfs_aops;
		inode->i_size = ezfs_inode->file_size;
		inode->i_blocks = ezfs_inode->nblocks * 8;
		set_nlink(inode, ezfs_inode->nlink);
		inode->i_atime = ezfs_inode->i_atime;
		inode->i_mtime = ezfs_inode->i_mtime;
		inode->i_ctime = ezfs_inode->i_ctime;
		i_uid_write(inode, ezfs_inode->uid);
		i_gid_write(inode, ezfs_inode->gid);
		unlock_new_inode(inode);
	}

	return inode;
}

static int ezfs_get_block(struct inode *inode, sector_t block,
			struct buffer_head *bh_result, int create)
{
	int ret, phys, i, ez_blk_n, ez_n_blk, idx_blk;
	struct super_block *sb = inode->i_sb;
	struct buffer_head *ezfs_sb_bh = get_ezfs_sb_bh(sb), *idx_blk_bh;
	struct ezfs_super_block *ezfs_sb = get_ezfs_sb(sb);
	struct ezfs_inode *ezfs_inode = get_ezfs_inode(inode);

	ret = phys = 0;
	ez_blk_n = ezfs_inode->direct_blk_n;
	idx_blk = ezfs_inode->indirect_blk_n;
	ez_n_blk = inode->i_blocks / 8;
	if (block && ez_n_blk > 1) {
		idx_blk_bh = sb_bread(sb, idx_blk);
		if (!idx_blk_bh)
			return -EIO;
		phys = *((uint64_t *) idx_blk_bh->b_data + block - 1);
		brelse(idx_blk_bh);
	} else if (!block && ez_n_blk)
		phys = ez_blk_n;
	if (phys) {
		map_bh(bh_result, sb, phys);
		return 0;
	}

	if (!create)
		return 0;

	/* The rest has to be protected against itself. */
	mutex_lock(ezfs_sb->ezfs_lock);

	for (i = 0; i < ezfs_spc(ezfs_sb) && IS_SET(ezfs_sb->free_data_blocks,
				i); ++i);
	if (i == ezfs_spc(ezfs_sb)) {
		ret = -ENOSPC;
		goto out;
	}

	phys = i + EZFS_ROOT_DATABLOCK_NUMBER;
	if (block) {
		if (ez_n_blk < 2) {
			for (idx_blk = i+1; idx_blk < ezfs_spc(ezfs_sb) &&
					IS_SET(ezfs_sb->free_data_blocks,
						idx_blk); ++idx_blk);
			if (idx_blk == ezfs_spc(ezfs_sb)) {
				ret = -ENOSPC;
				goto out;
			}
			idx_blk += EZFS_ROOT_DATABLOCK_NUMBER;
		}
		idx_blk_bh = sb_bread(sb, idx_blk);
		if (!idx_blk_bh) {
			ret = -EIO;
			goto out;
		}
		if (ez_n_blk < 2)
			memset(idx_blk_bh->b_data, 0, EZFS_BLOCK_SIZE);
		*(((uint64_t *) idx_blk_bh->b_data) + block - 1) = phys;
		mark_buffer_dirty(idx_blk_bh);
		brelse(idx_blk_bh);
		ezfs_inode->indirect_blk_n = idx_blk;
		SETBIT(ezfs_sb->free_data_blocks,
				idx_blk - EZFS_ROOT_DATABLOCK_NUMBER);
	} else
		ezfs_inode->direct_blk_n = phys;

	map_bh(bh_result, sb, phys);

	SETBIT(ezfs_sb->free_data_blocks, phys - EZFS_ROOT_DATABLOCK_NUMBER);
	mark_buffer_dirty(ezfs_sb_bh);

out:
	mutex_unlock(ezfs_sb->ezfs_lock);
	return ret;
}

/* ezfs_dir_ops */
int ezfs_iterate(struct file *filp, struct dir_context *ctx)
{
	int i, pos;
	struct inode *inode = file_inode(filp);
	uint64_t filp_blk_num = get_ezfs_inode(file_inode(filp))->direct_blk_n;
	struct buffer_head *bh = sb_bread(inode->i_sb, filp_blk_num);
	struct ezfs_dir_entry *ezfs_dentry;

	if (!dir_emit_dots(filp, ctx))
		return 0;
	pos = ctx->pos - 2;

	if (!bh)
		return -EIO;

	ezfs_dentry = (struct ezfs_dir_entry *) bh->b_data + pos;
	for (i = pos; i < EZFS_MAX_CHILDREN; ++i, ++ezfs_dentry, ++ctx->pos) {
		if (ezfs_dentry->active) {
			if (!dir_emit(ctx, ezfs_dentry->filename,
					strlen(ezfs_dentry->filename),
					ezfs_dentry->inode_no, DT_UNKNOWN))
				break;
		}
	}
	brelse(bh);

	return 0;
}

static void ezfs_truncate_blocks(struct inode *inode, loff_t old_size, loff_t offset)
{
	uint64_t *blk_ns;
	struct buffer_head *idx_blk_bh;

	int new_blocks = (inode->i_size + EZFS_BLOCK_SIZE - 1) / EZFS_BLOCK_SIZE;
	int old_blocks = (old_size + EZFS_BLOCK_SIZE - 1) / EZFS_BLOCK_SIZE;
	struct ezfs_super_block *ezfs_sb = get_ezfs_sb(inode->i_sb);
	struct ezfs_inode *ezfs_inode = get_ezfs_inode(inode);
	int i, data_blk_n = ezfs_inode->direct_blk_n;

	inode->i_blocks = new_blocks * 8;
	mutex_lock(ezfs_sb->ezfs_lock);
	if (!new_blocks) {
		if (data_blk_n)
			CLEARBIT(ezfs_sb->free_data_blocks,
					data_blk_n -
				EZFS_ROOT_DATABLOCK_NUMBER);
		ezfs_inode->direct_blk_n = 0;
		new_blocks++;
	}
	if (old_blocks > 1) {
		idx_blk_bh = sb_bread(inode->i_sb,
				ezfs_inode->indirect_blk_n);
		if (!idx_blk_bh) {
			mutex_unlock(ezfs_sb->ezfs_lock);
			return;
		}
		blk_ns = (uint64_t *) idx_blk_bh->b_data;
		for (i = new_blocks; i < old_blocks; ++i)
			if (*(blk_ns + i - 1))
				CLEARBIT(ezfs_sb->free_data_blocks,
					*(blk_ns + i - 1) -
				EZFS_ROOT_DATABLOCK_NUMBER);
		memset(blk_ns + new_blocks - 1, 0,
			(old_blocks - new_blocks) * sizeof(uint64_t));
		mark_buffer_dirty(idx_blk_bh);
		brelse(idx_blk_bh);
		if (new_blocks < 2) {
			CLEARBIT(ezfs_sb->free_data_blocks,
				ezfs_inode->indirect_blk_n -
				EZFS_ROOT_DATABLOCK_NUMBER);
			ezfs_inode->indirect_blk_n = 0;
		}
	}
	mutex_unlock(ezfs_sb->ezfs_lock);
	mark_buffer_dirty(get_ezfs_sb_bh(inode->i_sb));
	
	mark_inode_dirty(inode);
}

int ezfs_setattr(struct user_namespace *mnt_userns, struct dentry *dentry,
		   struct iattr *iattr)
{
	struct inode *inode = d_inode(dentry);
	int old_size = inode->i_size;
	int error;

	error = simple_setattr(mnt_userns, dentry, iattr);
	if (!error && old_size > inode->i_size)
		ezfs_truncate_blocks(inode, old_size, inode->i_size);
	return error;
}

/* ezfs_aops */
int ezfs_read_folio(struct file *file, struct folio *folio)
{
	return block_read_full_folio(folio, ezfs_get_block);
}

int ezfs_writepage(struct page *page, struct writeback_control *wbc)
{
	return block_write_full_page(page, ezfs_get_block, wbc);
}

static void ezfs_write_failed(struct address_space *mapping, loff_t to)
{
	if (to > mapping->host->i_size)
		truncate_pagecache(mapping->host, mapping->host->i_size);
}

int ezfs_write_begin(struct file *file, struct address_space *mapping,
		loff_t pos, unsigned int len,
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
	int ret, new_blocks;
	struct inode *inode = mapping->host;
	loff_t old_size = inode->i_size;

	ret = generic_write_end(file, mapping, pos, len, copied, page, fsdata);

	if (old_size != inode->i_size) {
		new_blocks = (inode->i_size + EZFS_BLOCK_SIZE - 1) / EZFS_BLOCK_SIZE;
		inode->i_blocks = 8 * new_blocks;
		mark_inode_dirty(inode);
	}
	return ret;
}

/* ezfs_inode_ops */
struct dentry *ezfs_lookup(struct inode *dir, struct dentry *child_dentry,
		unsigned int flags)
{
	int i;
	struct ezfs_dir_entry *ezfs_dentry;
	struct inode *inode = NULL;
	uint64_t dir_blk_num = get_ezfs_inode(dir)->direct_blk_n;
	struct buffer_head *dir_bh = sb_bread(dir->i_sb, dir_blk_num);

	if (!dir_bh)
		return ERR_PTR(-EIO);

	ezfs_dentry = (struct ezfs_dir_entry *) dir_bh->b_data;
	for (i = 0; i < EZFS_MAX_CHILDREN; ++i, ++ezfs_dentry) {
		if (ezfs_dentry->active &&
				child_dentry->d_name.len ==
				strlen(ezfs_dentry->filename) &&
				!memcmp(ezfs_dentry->filename,
					child_dentry->d_name.name,
					child_dentry->d_name.len)) {
			inode = ezfs_iget(dir->i_sb, ezfs_dentry->inode_no);
			break;
		}
	}
	brelse(dir_bh);

	return d_splice_alias(inode, child_dentry);
}

static void write_inode_helper(struct inode *inode, struct ezfs_inode *ezfs_inode)
{
	ezfs_inode->mode = inode->i_mode;
	ezfs_inode->file_size = inode->i_size;
	ezfs_inode->nlink = inode->i_nlink;
	ezfs_inode->i_atime = inode->i_atime;
	ezfs_inode->i_mtime = inode->i_mtime;
	ezfs_inode->i_ctime = inode->i_ctime;
	ezfs_inode->uid = inode->i_uid.val;
	ezfs_inode->gid = inode->i_gid.val;
	ezfs_inode->nblocks = inode->i_blocks / 8;
}

static struct inode *create_helper(struct inode *dir,
		struct dentry *dentry, umode_t mode, bool isdir)
{
	int i, i_idx, d_idx, i_num, d_num;
	struct ezfs_super_block *ezfs_sb = get_ezfs_sb(dir->i_sb);
	struct buffer_head *dir_bh, *i_bh;
	struct ezfs_dir_entry *ezfs_dentry;
	struct inode *new_inode, *ret = NULL;
	struct ezfs_inode *new_ezfs_inode;
	uint64_t dir_blk_num = get_ezfs_inode(dir)->direct_blk_n;

	if (strnlen(dentry->d_name.name, EZFS_MAX_FILENAME_LENGTH + 1) >
			EZFS_MAX_FILENAME_LENGTH) {
		pr_err("Filename too long\n");
		return ERR_PTR(-ENAMETOOLONG);
	}

	dir_bh = sb_bread(dir->i_sb, dir_blk_num);
	if (!dir_bh)
		return ERR_PTR(-EIO);

	ezfs_dentry = (struct ezfs_dir_entry *) dir_bh->b_data;
	for (i = 0; i < EZFS_MAX_CHILDREN && ezfs_dentry->active; ++i, ++ezfs_dentry);
	if (i == EZFS_MAX_CHILDREN) {
		brelse(dir_bh);
		return ERR_PTR(-ENOSPC);
	}

	mutex_lock(ezfs_sb->ezfs_lock);
	/* find an empty inode */
	for (i_idx = 0; i_idx < EZFS_MAX_INODES && IS_SET(ezfs_sb->free_inodes, i_idx); i_idx++);
	if (i_idx == EZFS_MAX_INODES) {
		ret = ERR_PTR(-ENOSPC);
		goto out;
	}
	i_num = i_idx + EZFS_ROOT_INODE_NUMBER;

	/*
	 * empty regular files don't need data block
	 * find an empty data block for the created folder
	 */
	if (isdir)
		mode |= S_IFDIR;

	if (mode & S_IFDIR) {
		struct buffer_head *new_dir_bh;

		for (d_idx = 0; d_idx < ezfs_spc(ezfs_sb) && IS_SET(
					ezfs_sb->free_data_blocks, d_idx);
				d_idx++);
		if (d_idx == ezfs_spc(ezfs_sb)) {
			ret = ERR_PTR(-ENOSPC);
			goto out;
		}
		d_num = d_idx + EZFS_ROOT_DATABLOCK_NUMBER;
		/* folder data block should be zeroed out */
		new_dir_bh = sb_bread(dir->i_sb, d_num);
		if (!new_dir_bh) {
			ret = ERR_PTR(-EIO);
			goto out;
		}
		memset(new_dir_bh->b_data, 0, EZFS_BLOCK_SIZE);
		mark_buffer_dirty(new_dir_bh);
		brelse(new_dir_bh);
	}

	new_inode = iget_locked(dir->i_sb, i_num);
	if (IS_ERR(new_inode)) {
		ret = new_inode;
		goto out;
	}

	/* initialize new inode & ezfs_inode */
	i_bh = get_ezfs_i_bh(dir->i_sb);
	new_ezfs_inode = ((struct ezfs_inode *) i_bh->b_data) + i_idx;
	new_inode->i_mode = mode;
	new_inode->i_op = &ezfs_inode_ops;
	new_inode->i_sb = dir->i_sb;
	if (new_inode->i_mode & S_IFDIR) {
		new_inode->i_fop = &ezfs_dir_ops;
		new_inode->i_size = EZFS_BLOCK_SIZE;
		new_inode->i_blocks = 8;
		new_ezfs_inode->direct_blk_n = d_num;
		set_nlink(new_inode, 2);
	} else {
		new_inode->i_fop = &ezfs_file_ops;
		new_inode->i_size = 0;
		new_inode->i_blocks = 0;
		new_ezfs_inode->direct_blk_n = 0;
		set_nlink(new_inode, 1);
	}
	new_ezfs_inode->indirect_blk_n = 0;
	new_inode->i_mapping->a_ops = &ezfs_aops;
	new_inode->i_atime = new_inode->i_mtime = new_inode->i_ctime = current_time(new_inode);
	inode_init_owner(&init_user_ns, new_inode, dir, mode);

	write_inode_helper(new_inode, new_ezfs_inode);
	mark_buffer_dirty(i_bh);
	new_inode->i_private = (void *) new_ezfs_inode;

	d_instantiate_new(dentry, new_inode);
	mark_inode_dirty(new_inode);

	/* write new dentry to dir data block */
	strncpy(ezfs_dentry->filename, dentry->d_name.name,
					strlen(dentry->d_name.name));
	ezfs_dentry->active = 1;
	ezfs_dentry->inode_no = i_num;
	mark_buffer_dirty(dir_bh);

	/* update dir inode attributes */
	dir->i_mtime = dir->i_ctime = current_time(dir);
	if (new_inode->i_mode & S_IFDIR)
		inc_nlink(dir);
	mark_inode_dirty(dir);

	SETBIT(ezfs_sb->free_inodes, i_idx);
	if (mode & S_IFDIR)
		SETBIT(ezfs_sb->free_data_blocks, d_idx);
	mark_buffer_dirty(get_ezfs_sb_bh(dir->i_sb));
out:
	brelse(dir_bh);
	mutex_unlock(ezfs_sb->ezfs_lock);
	return ret;
}

int ezfs_create(struct user_namespace *mnt_usrns, struct inode *dir,
	struct dentry *dentry, umode_t mode, bool excl)
{
	struct inode *inode;

	inode = create_helper(dir, dentry, mode, false);
	if (IS_ERR(inode))
		return PTR_ERR(inode);

	return 0;
}

static int ezfs_unlink_dentry(struct buffer_head *bh, struct dentry *dentry)
{
	int i, ret = 0;
	struct ezfs_dir_entry *ezfs_dentry = (struct ezfs_dir_entry *) bh->b_data;

	for (i = 0; i < EZFS_MAX_CHILDREN; ++i, ++ezfs_dentry) {
		if (ezfs_dentry->active && strlen(ezfs_dentry->filename) &&
			!memcmp(ezfs_dentry->filename,
			dentry->d_name.name, dentry->d_name.len)) {
			memset(ezfs_dentry, 0, sizeof(struct ezfs_dir_entry));
			mark_buffer_dirty(bh);
			ret = 1;
			goto out;
		}
	}

out:
	brelse(bh);
	return ret;
}

int ezfs_unlink(struct inode *dir, struct dentry *dentry)
{
	struct inode *inode = d_inode(dentry);
	uint64_t dir_blk_num = get_ezfs_inode(dir)->direct_blk_n;
	struct buffer_head *bh = sb_bread(dir->i_sb, dir_blk_num);

	if (!bh)
		return -EIO;

	if (ezfs_unlink_dentry(bh, dentry)) {
		inode->i_ctime = dir->i_ctime = dir->i_mtime = current_time(inode);
		drop_nlink(inode);
		mark_inode_dirty(dir);
	}

	return 0;
}

int ezfs_mkdir(struct user_namespace *mnt_usrns, struct inode *dir, struct dentry *dentry, umode_t mode)
{
	struct inode *inode;

	inode = create_helper(dir, dentry, mode, true);
	if (IS_ERR(inode))
		return PTR_ERR(inode);

	return 0;
}

static int ezfs_dir_empty(struct buffer_head *bh)
{
	int i, ret = 1;
	struct ezfs_dir_entry *dentry = (struct ezfs_dir_entry *)bh->b_data;

	for (i = 0; i < EZFS_MAX_CHILDREN; ++i, ++dentry) {
		if (dentry->active) {
			ret = 0;
			break;
		}
	}
	brelse(bh);
	return ret;
}

int ezfs_rmdir(struct inode *dir, struct dentry *dentry)
{
	struct buffer_head *dir_bh = sb_bread(dir->i_sb,
			get_ezfs_inode(d_inode(dentry))->direct_blk_n);

	if (!dir_bh)
		return -EIO;

	if (!ezfs_dir_empty(dir_bh))
		return -ENOTEMPTY;

	/* the directory is empty, rmdir */
	ezfs_unlink(dir, dentry);
	/* drop nlink for . */
	drop_nlink(d_inode(dentry));
	/* drop nlink for .. */
	drop_nlink(dir);
	return 0;
}

/* ezfs_sb_ops */
void ezfs_evict_inode(struct inode *inode)
{
	int i, ez_n_blk = inode->i_blocks / 8;
	uint64_t *blk_ns;
	struct buffer_head *sb_bh = get_ezfs_sb_bh(inode->i_sb), *idx_blk_bh;
	struct ezfs_super_block *ezfs_sb = get_ezfs_sb(inode->i_sb);
	struct ezfs_inode *ezfs_inode = get_ezfs_inode(inode);

	mutex_lock(ezfs_sb->ezfs_lock);
	if (!inode->i_nlink) {
		int data_blk_num = ezfs_inode->direct_blk_n;

		if (ez_n_blk > 1) {
			idx_blk_bh = sb_bread(inode->i_sb,
					ezfs_inode->indirect_blk_n);
			if (!idx_blk_bh) {
				mutex_unlock(ezfs_sb->ezfs_lock);
				return;
			}
			blk_ns = (uint64_t *) idx_blk_bh->b_data;
			for (i = 0; i < ez_n_blk - 1; ++i)
				if (*(blk_ns + i))
					CLEARBIT(ezfs_sb->free_data_blocks,
							*(blk_ns + i) -
						EZFS_ROOT_DATABLOCK_NUMBER);
			brelse(idx_blk_bh);
			CLEARBIT(ezfs_sb->free_data_blocks,
					ezfs_inode->indirect_blk_n -
					EZFS_ROOT_DATABLOCK_NUMBER);
		}
		if (data_blk_num)
			CLEARBIT(ezfs_sb->free_data_blocks, data_blk_num -
					EZFS_ROOT_DATABLOCK_NUMBER);
		CLEARBIT(ezfs_sb->free_inodes, inode->i_ino -
				EZFS_ROOT_INODE_NUMBER);
		mark_buffer_dirty(sb_bh);
	}

	/* required to be called by VFS, if not called, evict() will BUG out */
	truncate_inode_pages_final(&inode->i_data);
	invalidate_inode_buffers(inode);
	clear_inode(inode);
	mutex_unlock(ezfs_sb->ezfs_lock);
}

int ezfs_write_inode(struct inode *inode, struct writeback_control *wbc)
{
	int ret = 0;
	struct buffer_head *i_bh = get_ezfs_i_bh(inode->i_sb);

	write_inode_helper(inode, get_ezfs_inode(inode));
	mark_buffer_dirty(i_bh);
	if (wbc->sync_mode == WB_SYNC_ALL) {
		sync_dirty_buffer(i_bh);
		if (buffer_req(i_bh) && !buffer_uptodate(i_bh))
			return -EIO;
	}

	return ret;
}

static int ezfs_fill_super(struct super_block *sb, struct fs_context *fc)
{
	struct buffer_head *bh;
	struct ezfs_sb_buffer_heads *ezfs_sb_bufs = sb->s_fs_info;
	struct ezfs_super_block *ezfs_sb;
	struct inode *inode;

	sb->s_maxbytes		= EZFS_BLOCK_SIZE * EZFS_MAX_DATA_BLKS;
	sb->s_magic			= EZFS_MAGIC_NUMBER;
	sb->s_op			= &ezfs_sb_ops;
	sb->s_time_gran		= 1;
	sb->s_time_min		= 0;
	sb->s_time_max		= U32_MAX;

	/* if ezfs_fill_super fails, ezfs_free_fc will free allocated resources */
	if (!sb_set_blocksize(sb, EZFS_BLOCK_SIZE))
		return -EIO;
	bh = sb_bread(sb, EZFS_SUPERBLOCK_DATABLOCK_NUMBER);
	if (!bh)
		return -EIO;
	ezfs_sb_bufs->sb_bh = bh;

	ezfs_sb = get_ezfs_sb(sb);
	ezfs_sb->ezfs_lock = kzalloc(sizeof(struct mutex), GFP_KERNEL);
	if (!ezfs_sb->ezfs_lock)
		return -ENOMEM;
	mutex_init(ezfs_sb->ezfs_lock);
	if (ezfs_sb->magic != EZFS_MAGIC_NUMBER)
		return -EIO;
	if (ezfs_sb->disk_blks <= 2)
		return -EIO;
	sb->s_maxbytes = EZFS_BLOCK_SIZE * MIN(ezfs_sb->disk_blks - 2, 513);

	bh = sb_bread(sb, EZFS_INODE_STORE_DATABLOCK_NUMBER);
	if (!bh)
		return -EIO;
	ezfs_sb_bufs->i_store_bh = bh;

	inode = ezfs_iget(sb, EZFS_ROOT_INODE_NUMBER);
	if (IS_ERR(inode))
		return PTR_ERR(inode);

	sb->s_root = d_make_root(inode);
	if (!sb->s_root)
		return -ENOMEM;

	return 0;
}

static void ezfs_free_fc(struct fs_context *fc)
{
	struct ezfs_sb_buffer_heads *ezfs_sb_bufs = fc->s_fs_info;
	struct ezfs_super_block *ezfs_sb;

	if (ezfs_sb_bufs) {
		if (ezfs_sb_bufs->i_store_bh)
			brelse(ezfs_sb_bufs->i_store_bh);
		if (ezfs_sb_bufs->sb_bh) {
			ezfs_sb = (struct ezfs_super_block *) ezfs_sb_bufs->sb_bh->b_data;
			if (ezfs_sb->ezfs_lock) {
				mutex_destroy(ezfs_sb->ezfs_lock);
				kfree(ezfs_sb->ezfs_lock);
			}
			brelse(ezfs_sb_bufs->sb_bh);
		}
		kfree(ezfs_sb_bufs);
	}
}

static int ezfs_get_tree(struct fs_context *fc)
{
	return get_tree_bdev(fc, ezfs_fill_super);
}

static const struct fs_context_operations ezfs_context_ops = {
	.free		= ezfs_free_fc,
	.get_tree	= ezfs_get_tree,
};

int ezfs_init_fs_context(struct fs_context *fc)
{
	struct ezfs_sb_buffer_heads *ezfs_sb_bufs;

	ezfs_sb_bufs = kzalloc(sizeof(*ezfs_sb_bufs), GFP_KERNEL);
	if (!ezfs_sb_bufs)
		return -ENOMEM;
	fc->s_fs_info = ezfs_sb_bufs;
	fc->ops = &ezfs_context_ops;
	return 0;
}

static void ezfs_kill_superblock(struct super_block *sb)
{
	struct ezfs_sb_buffer_heads *ezfs_sb_bufs = sb->s_fs_info;
	struct ezfs_super_block *ezfs_sb = (struct ezfs_super_block *)
										ezfs_sb_bufs->sb_bh->b_data;

	kill_block_super(sb);
	brelse(ezfs_sb_bufs->i_store_bh);
	mutex_destroy(ezfs_sb->ezfs_lock);
	kfree(ezfs_sb->ezfs_lock);
	brelse(ezfs_sb_bufs->sb_bh);
	kfree(ezfs_sb_bufs);
}

struct file_system_type ezfs_fs_type = {
	.owner = THIS_MODULE,
	.name = "ezfs",
	.init_fs_context = ezfs_init_fs_context,
	.kill_sb = ezfs_kill_superblock,
};

static int ezfs_init(void)
{
	return register_filesystem(&ezfs_fs_type);
}

static void ezfs_exit(void)
{
	unregister_filesystem(&ezfs_fs_type);
}

module_init(ezfs_init);
module_exit(ezfs_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sol");
