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
#include "include/device.h"
#include "include/rcu.h"

ssize_t bldms_read(struct file *filp, char __user *buf, size_t len, loff_t *off){
	struct buffer_head *bh = NULL;
	struct inode *f_inode = filp->f_inode;
	uint64_t file_sz = f_inode->i_size;
	int ret;
	loff_t offset;
	uint32_t block_to_read, device_blk;
	rcu_elem *rcu_el, *next_el;
	ktime_t *next_ts, *old_session_metadata;

	//printk("%s: read operation called with len %ld - and offset %lld (file size is %lld)", MOD_NAME, len, *off, file_sz);

	/*
	 * this operation is not synchronized
	 * *off can be changed concurrently
	 * add synchronization if you need it for any reason
	 */

	// check that *off is within boundaries
	if ((*off + NUM_METADATA_BLKS * DEFAULT_BLOCK_SIZE) >= file_sz){
		return 0;
	} else if ((*off + NUM_METADATA_BLKS * DEFAULT_BLOCK_SIZE) + len > file_sz){
		len = file_sz - *off;
	}

	// determine the offset inside the block to be read
	offset = *off % DEFAULT_BLOCK_SIZE;

	// if the offset is inside the metadata part of the block, shift it
	if (offset < METADATA_SIZE){
		// reduce the length of the shifted bytes of the offset
		//len -= (METADATA_SIZE - offset);
		*off += (METADATA_SIZE - offset);
		offset = METADATA_SIZE;
	}

	// just read stuff in a single block, residuals will be managed at application level
	if(offset + len > DEFAULT_BLOCK_SIZE){
		len = DEFAULT_BLOCK_SIZE - offset;
	}

	// compute the index of the block to be read (skipping superblocks and initial metadata blocks)
	device_blk = *off / DEFAULT_BLOCK_SIZE;
	block_to_read = device_blk + NUM_METADATA_BLKS;
	printk("%s: read() operation must access block %d of the device",MOD_NAME, device_blk);

	/* flag RCU read-side critical section beginning */
	rcu_read_lock();
	next_ts = (ktime_t *)filp->private_data;
	list_for_each_entry_rcu(rcu_el, &valid_blk_list, node){
		
		if (rcu_el->ndx == device_blk){
			// the block has been found in the RCU list, so it is valid
			pr_info("%s: read() found valid block in RCU list with index %d\n", MOD_NAME, rcu_el->ndx);
			break;		
		}else if (rcu_el->nsec > *next_ts){
			/*
			* The searched block has been invalidated between different read() calls:
			* since the RCU list is timestamp ordered, finding a node with timestamp greater
			* of the expected one means that the searched block is not in the RCU list anymore. 
			* So, let's read the first element of the RCU list with timestamp bigger of the expected one, if any. 
			*/

			// pr_info("%s: else branch hit - index %d - block_to_read %u\n", MOD_NAME, rcu_el->ndx, block_to_read);
			// pr_info("%s: else branch hit - block ts %lld - session ts %lld\n", MOD_NAME, rcu_el->nsec, *next_ts);
			device_blk = rcu_el->ndx;
			block_to_read = device_blk + NUM_METADATA_BLKS;
			*off = (device_blk * DEFAULT_BLOCK_SIZE) + METADATA_SIZE;
			offset = METADATA_SIZE;
			len = (rcu_el->valid_bytes < len) ? rcu_el->valid_bytes : len;
			break; 
		}
	}

	// rcu_el is the element to be read

	// to check if the tail of the list has been reached, we have to check if the node points to the head of the list (circular behaviour)
	if (&(rcu_el->node) == &valid_blk_list){
		// there is no valid node left to read
		pr_info("%s: no more messages hit (rcu_el is end of the list)\n", MOD_NAME);
		ret = 0;
		goto end_of_msgs;
	}


	if (offset - METADATA_SIZE > rcu_el->valid_bytes){
		// this block has already been read; go to the next one
		goto set_next_blk;

	}else if (len + offset - METADATA_SIZE > rcu_el->valid_bytes){
		// len exceeds the valid bytes, need to resize it
		len = rcu_el->valid_bytes - (offset - METADATA_SIZE);
	}

	// read the block and cache it in the buffer head
	bh = sb_bread(filp->f_path.dentry->d_inode->i_sb, block_to_read);
	if(!bh){
		rcu_read_unlock();
		return -EIO;
	}

	// copy the block into a user space buffer
	ret = copy_to_user(buf, bh->b_data + offset, len);

	if (ret != 0 || offset + len - ret < rcu_el->valid_bytes + METADATA_SIZE){
		// the block content has not been read completely: no need to update session
		*off += (len - ret);
		brelse(bh);
		// return the number of residual bytes in the block
		rcu_read_unlock();
		pr_info("%s: block has not been read completely - copy_to_user() return value = %d\n", MOD_NAME, ret);
		return (len - ret);
	}
	brelse(bh);
	ret = (len - ret);

set_next_blk:
	// get the next element in the RCU list (the next, in timestamp order, valid block)
	next_el = rcu_next_elem(rcu_el);
	if (&(next_el->node) == &valid_blk_list){
		goto end_of_msgs;
	}

	// update the session metadata: call with GFP_ATOMIC to avoid sleeping
	next_ts = kzalloc(sizeof(ktime_t), GFP_ATOMIC);
	if(!next_ts){
		rcu_read_unlock();
		return -ENOMEM;
	}
	*next_ts = next_el->nsec;
	old_session_metadata = (ktime_t *) filp->private_data;
	filp->private_data = (void *)next_ts;
	kfree(old_session_metadata);

	/*
	* set the offset to the beginning of data of the next valid block:
	* this is not strictly necessary, since the message delivered on the next call
	* will be typically determined by the timestamp registered in the session structure
	*/
	*off = (next_el->ndx * DEFAULT_BLOCK_SIZE) + METADATA_SIZE;


	// signal the end of the RCU read-side critical section
	rcu_read_unlock();
 
	// return the number of READ bytes, not the residuals
	return ret;

end_of_msgs:

	/*
	* If there are no more messages to be delivered, set the offset equal to the size of the file,
	* in order to make the caller finish.
	* Signal the end of the RCU read-side critical section.
	* */

	*off = file_sz;
	rcu_read_unlock();
	return (ret > 0) ? ret : 0;
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
		inode_init_owner(sb->s_user_ns, var_inode, NULL, S_IFDIR);
#else
		inode_init_owner(var_inode, NULL, S_IFDIR);
#endif

		var_inode->i_mode = S_IFREG | S_IRUSR | S_IRGRP | S_IROTH | S_IWUSR | S_IWGRP | S_IWOTH;
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

		fs_specific_inode = (struct bldms_inode *) bh->b_data;
		// setting the right size reading it from the fs specific file size
		var_inode->i_size = fs_specific_inode->file_size;
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

int bldms_open(struct inode *inode, struct file *filp){
	ktime_t *nsec;
	if(!bldms_mounted){
		return -ENODEV;
	}
	// increment module usage count
	try_module_get(THIS_MODULE);

	if ((filp->f_flags & O_ACCMODE) == O_RDONLY || (filp->f_flags & O_ACCMODE) == O_RDWR){
		// initialize the I/O session private data: timestamp of the next valid block to be read; init to 0;
		nsec = kzalloc(sizeof(ktime_t), GFP_ATOMIC);
		if(!nsec)
			return -ENOMEM;
		// should already be set to 0, but who knows?! :)
		*nsec = 0;
		filp->private_data = (void *)nsec;
		pr_info("%s: the device has been opened in RDONLY mode; session's private data initialized\n", MOD_NAME);
	}

	inode->i_size = filp->f_inode->i_size;	
	return 0;
}

int bldms_release(struct inode *inode, struct file *filp){
	if(!bldms_mounted){
		return -ENODEV;
	}
	
	if ((filp->f_flags & O_ACCMODE) == O_RDONLY || (filp->f_flags & O_ACCMODE) == O_RDWR){
		if(filp->private_data){
			kfree(filp->private_data);
		}
	}
	
	// decrement the module usage count
	module_put(THIS_MODULE);
	pr_info("%s: someone called a release on the device; it has been executed correctly\n", MOD_NAME);
	return 0;
}

loff_t bldms_llseek(struct file *filp, loff_t off, int whence){
	ktime_t *nsec, *old_nsec;

	switch(whence){
		case SEEK_SET:
			if(off == 0 && filp->private_data != NULL){
				nsec = kzalloc(sizeof(ktime_t), GFP_ATOMIC);
				if (!nsec){
					printk("%s: llseek() invoked but returned error in allocation of memory\n", MOD_NAME);
					return -ENOMEM;
				}
				*nsec = 0;
				//TODO: change this to an atomic exchange and mfence
				old_nsec = filp->private_data;
				filp->private_data = nsec;
				filp->f_pos = 0;
				kfree(old_nsec);
				printk("%s: llseek() invoked - timestamp saved in the session has been reset\n", MOD_NAME);
			}else{
				printk("%s: llseek() not allowed on offset different from zero or on file not opened in read mode\n", MOD_NAME);
				return -EINVAL;
			} 
			return 0;

		default:
			printk("%s: llseek() error - only SEEK_SET at the very beginning of the file is permitted\n", MOD_NAME);
			break;
	}

	return -EINVAL;
}

// look up goes in the inode operations
const struct inode_operations bldms_inode_ops = {
	.lookup = bldms_lookup,
};

const struct file_operations bldms_file_operations = {
	.owner = THIS_MODULE,
	.read = bldms_read,
	.open = bldms_open,
	.release = bldms_release,
	.llseek = bldms_llseek
};
