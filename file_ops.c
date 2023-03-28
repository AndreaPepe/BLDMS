#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/timekeeping.h>
#include <linux/time.h>
#include <linux/buffer_head.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/version.h>

#include "include/bldms.h"

#define NUM_METADATA_BLKS 1 // FIXME:for now only the superblock

ssize_t bldms_read(struct file *filp, char __user *buf, size_t len, loff_t *off){
	struct buffer_head *bh = NULL;
	struct inode *f_inode = filp->f_inode;
	uint64_t file_sz = f_inode->i_size;
	int ret;
	loff_t offset;
	int block_to_read;

	printk("%s: read operation called with len %ld - and offset %lld (file size is %ld)", MOD_NAME, len, *off, file_sz);

	/*
	 * this operation is not synchronized
	 * *off can be changed concurrently
	 * add synchronization if you need it for any reason
	 */

	// check that *off is within boundaries
	if (*off >= file_sz){
		return 0;
	} else if (*off + len > file_sz){
		len = file_sz - *off;
	}

	// determine the offset inside the block to be read
	offset = *off % DEFAULT_BLOCK_SIZE;

	// just read stuff in a single block, residuals will be managed at application level
	if(offset + len > DEFAULT_BLOCK_SIZE){
		len = DEFAULT_BLOCK_SIZE - offset;
	}

	// compute the index of the block to be read (skipping superblocks and initial metadata blocks)
	block_to_read = *off / DEFAULT_BLOCK_SIZE + NUM_METADATA_BLKS;
	printk("%s: read operation must access block %d of the device",MOD_NAME, block_to_read);

	// read the block and cache it in the buffer head
	bh = sb_bread(filp->f_path.dentry->d_inode->i_sb, block_to_read);
	if(!bh){
		return -EIO;
	}

	// copy the block into a user space buffer
	ret = copy_to_user(buf, bh->b_data + offset, len);
	*off = len - ret;
	brelse(bh);

	// return the number of residuals bytes 
	return len - ret;
}


struct dentry *bldms_lookup(struct inode *parent_inode, struct dentry *child_dentry, unsigned int flags){
	struct bldms_inode *fs_specific_inode;
	struct super_block *sb = parent_inode->i_sb;
	struct buffer_head *bh = NULL;
	struct inode *var_inode;

	printk("%s: running the lookup inode-function for name %s",MOD_NAME,child_dentry->d_name.name);

	if(!strcmp(UNIQUE_FILE_NAME, child_dentry->d_name.name)){
		// only for the unique file of the fs

		// get a locked inode from the cache
		var_inode = iget_locked(sb, 1);
		if (!var_inode){
			return ERR_PTR(-ENOMEM);
		}

		// if the inode was already cached, simply return it
		if(!(var_inode->i_state & I_NEW)){
			return child_dentry;
		}

		// if the inode was not already cached
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 12, 0)
		inode_init_owner(NULL, var_inode, NULL, S_IFDIR);
#else
		inode_init_owner(var_inode, NULL, S_IFDIR);
#endif

		var_inode->i_mode = S_IFREG | S_IRUSR | S_IRGRP | S_IROTH | S_IWUSR | S_IWGRP | S_IXUSR | S_IXGRP | S_IXOTH;
        var_inode->i_fop = &bldms_file_operations;
		var_inode->i_op = &bldms_inode_ops;	

		// set the number of links for this file
		set_nlink(var_inode, 1);

		// retrieve the file size via the FS specific inode and put it into the generic inode
		bh = sb_bread(sb, BLDMS_INODES_BLOCK_NUMBER);
		if(!bh){
			iput(var_inode);
			return ERR_PTR(-EIO);
		}

		fs_specific_inode = bh->b_data;
		var_inode->i_size = bh->b_size;
		brelse(bh);

		// add dentry to the hash queue and init inode
		d_add(child_dentry, var_inode);
		
		// increment reference count of the dentry
		dget(child_dentry);

		// unlock the inode to make it usable
		unlock_new_inode(var_inode);
		return child_dentry;
	}

	return NULL;
}


// look up goes in the inode operations
const struct inode_operations bldms_inode_ops = {
	.lookup = bldms_lookup,
};

const struct file_operations bldms_file_operations = {
	.owner = THIS_MODULE,
	.read = bldms_read,
	//.write = onefilefs_write //please implement this function to complete the exercise
};
