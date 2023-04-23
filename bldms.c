/**
 * Copyright (C) 2023 Andrea Pepe <pepe.andmj@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * @file bldms.c
 * 
 * @brief Block-Level Data Management Service (BLDMS) for Linux module.
 * This specific file contains the module initialization and cleanup function
 * for the registration of a single-file file system and the driver for the block
 * device represented by the single file. The driver is partially made of VFS-operations
 * and partially of system calls. The installation of the system calls on free-entries
 * of the system call table is done relying on the USCTM module for the discovery of the 
 * system call table position.
 * 
 * @author Andrea Pepe
 * @date April 22, 2023  
*/

#include <linux/module.h>
#include <linux/printk.h>
#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/buffer_head.h>
#include <linux/version.h>
#include <linux/timekeeping.h>
#include <linux/string.h>
#include <linux/blkdev.h>
#include <linux/vmalloc.h>

#include "include/bldms.h"
#include "include/rcu.h"
#include "include/syscalls.h"

/* Declaration of global variables for the device management */
unsigned char bldms_mounted = 0;
bldms_block **metadata_array;
size_t md_array_size;
uint32_t last_written_block = 0;
struct super_block *the_dev_superblock;


static struct super_operations bldms_fs_super_ops = {
};

static struct dentry_operations bldms_fs_dentry_ops = {
};


int bldms_fs_fill_super(struct super_block *sb, void *data, int silent){

    struct inode *root_inode;
    struct bldms_inode *the_file_inode;
    struct buffer_head *bh;
    struct bldms_sb_info *sb_info;
    uint64_t magic;
    struct timespec64 curr_time;
    int i, ret;
    rcu_elem *rcu_el;

    // assign the magic number that identifies the FS
    sb->s_magic = MAGIC;

    // read the superblock at index SB_BLOCK_NUMBER
    bh = sb_bread(sb, SB_BLOCK_NUMBER);
    if(!sb){
        return -EIO;
    }

    // read the magic number from the device
    sb_info = (struct bldms_sb_info *) bh->b_data;
    magic = sb_info->magic;
    brelse(bh);

    // check if the magic number corresponds to the expected one
    if (magic != sb->s_magic){
        return -EBADF;
    }

    // set file-system specific info and operations
    sb->s_fs_info = NULL;
    sb->s_op = &bldms_fs_super_ops;

    root_inode = iget_locked(sb, 0);
    if (!root_inode){
        return -ENOMEM;
    }


    root_inode->i_ino = ROOT_INODE_NUMBER;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 12, 0)
    inode_init_owner(sb->s_user_ns, root_inode, NULL, S_IFDIR);
#else
    inode_init_owner(root_inode, NULL, S_IFDIR);
#endif

    root_inode->i_sb = sb;

    // setup root inode and file ops
    root_inode->i_op = &bldms_inode_ops;
    root_inode->i_fop = &bldms_dir_operations;

    // update access permissions
    root_inode->i_mode = S_IFDIR | S_IRUSR | S_IRGRP | S_IROTH | S_IWUSR | S_IWGRP | S_IXUSR | S_IXGRP | S_IXOTH;

    // baseline alignment of the FS timestamp to the current time
    ktime_get_real_ts64(&curr_time);
    root_inode->i_atime = root_inode->i_mtime = root_inode->i_ctime = curr_time;

    // no inode from device is needed: the root of the FS is an in-memory object
    root_inode->i_private = NULL;

    // create dentry for the root of file system
    sb->s_root = d_make_root(root_inode);
    if (!sb->s_root){
        return -ENOMEM;
    }
    
    // setup dentry operations
    sb->s_root->d_op = &bldms_fs_dentry_ops;

    // unlock the inode to make it usable
    unlock_new_inode(root_inode);


    bh = sb_bread(sb, BLDMS_SINGLEFILE_INODE_NUMBER);
    if(!bh){
        return -EIO;
    }
    
    /*
    * Check on the maximum number of manageable blocks
    * Since the device is actually a file, the FS-mount operation corresponds to the device-mount operation,
    * so such check is performed here. 
    * */
    the_file_inode = (struct bldms_inode *) bh->b_data;
    
    if (the_file_inode->file_size > NBLOCKS * DEFAULT_BLOCK_SIZE){
        // unamangeable block: too big
        printk("%s: mounting error - the device has %llu blocks, while NBLOCKS is %d\n", MOD_NAME, the_file_inode->file_size / DEFAULT_BLOCK_SIZE, NBLOCKS);
        return -E2BIG;
    }
    
    // compute the number of blocks of the device and init an array of blocks metadata
    md_array_size = the_file_inode->file_size / DEFAULT_BLOCK_SIZE;
    brelse(bh);

    printk("%s: the device has %lu blocks\n", MOD_NAME, md_array_size);

    // this is a temp array used to order blocks by ascending timestamp, in order to place them in order in the RCU list
    if(sizeof(bldms_block *) * md_array_size > 1024 * PAGE_SIZE){
        // kzalloc can only allocate up to 4MB; if bigger, use vzalloc
        metadata_array = vzalloc(sizeof(bldms_block *) * md_array_size);
        if(!metadata_array){
            return -EINVAL;
        }
    }else{
        metadata_array = kzalloc(sizeof(bldms_block *) * md_array_size, GFP_ATOMIC);
        if(!metadata_array){
            return -EINVAL;
        }
    }

    /*
    * Initialize data structures and RCU-list for device's block mapping and management:
    * an array of metadata for each block of the device is maintained, while in the RCU list
    * will be placed only the actually valid blocks, i.e. containing valid messages.
    * The RCU list is kept ordered timestamp-wise.
    */
    rcu_init();
    for (i = 0; i < md_array_size; i++){
        bh = sb_bread(sb, i + NUM_METADATA_BLKS);
        metadata_array[i] = kzalloc(sizeof(bldms_block), GFP_ATOMIC);
        if (!metadata_array[i]){
            i--;
            ret = -ENOMEM;
            goto err_and_clean_rcu;
        }
        if (!bh){
            // when error, free the allocated data structure before returning
            ret = -EIO;
            goto err_and_clean_rcu;
        }       
        memcpy(metadata_array[i], bh->b_data, sizeof(bldms_block));
        brelse(bh);

        // if it's a valid block, also insert it into the initial RCU list
        if (metadata_array[i]->is_valid == BLK_VALID){
            pr_info("%s: Block of index %u is valid - it has timestamp %lld, valid bytes %u and is_valid %d\n", MOD_NAME, i,
                 metadata_array[i]->nsec, metadata_array[i]->valid_bytes, metadata_array[i]->is_valid);
            rcu_el = kzalloc(sizeof(rcu_elem), GFP_ATOMIC);
            if(!rcu_el){
                ret = -ENOMEM;
                goto err_and_clean_rcu;
            }
            /*
            * Initialize the RCU list of valid blocks, by pushing in order the blocks
            * already present and valid found on the device.
            * The RCU list will always be kept in timestamp order. 
            */
            add_valid_block_in_order_secure(rcu_el, i, metadata_array[i]->valid_bytes, metadata_array[i]->nsec);
        }
    }

    
    // the number of the last valid block is saved to be used as a reference for finding the next free block to be written
    last_written_block = (!list_empty(&valid_blk_list)) ? (list_last_entry(&valid_blk_list, rcu_elem, node)->ndx) : (md_array_size - 1);

    // signal that the device (with the file system) has been mounted
    bldms_mounted = 1;

    return 0;

err_and_clean_rcu:
    // no need to be RCU-safe here, since no one can actually access the list in initialization phase
    list_for_each_entry(rcu_el, &valid_blk_list, node){
        list_del(&(rcu_el->node));
        kfree(rcu_el);
    }

    for(; i >= 0; i--){
        kfree(metadata_array[i]);
    }

    if(sizeof(bldms_block *) * md_array_size > 1024 * PAGE_SIZE){
        vfree(metadata_array);
    }else{
        kfree(metadata_array);
    }

    return ret;
}


static inline void free_data_structures(void){
    // take the spinlock and release it only when all rcu elements are safely deleted from the list
    spin_lock(&rcu_write_lock);
    remove_all_entries_secure();
    

    if(sizeof(bldms_block *) * md_array_size > 1024 * PAGE_SIZE){
        vfree(metadata_array);
    }else{
        kfree(metadata_array);
    }

    the_dev_superblock = NULL;
    bldms_mounted = 0;
    spin_unlock(&rcu_write_lock);
}


static void bldms_fs_kill_sb(struct super_block *sb){
    kill_block_super(sb);
    
    if(the_dev_superblock)
        blkdev_put(the_dev_superblock->s_bdev, FMODE_READ | FMODE_WRITE);

    free_data_structures();
    
    printk(KERN_INFO "%s: file system unmounted successfully\n", MOD_NAME);
    return;
}


// Called on mount operations
struct dentry *bldms_fs_mount(struct file_system_type *fs_type, int flags, const char *dev_name, void *data){
    struct dentry *ret;
    struct block_device *the_device;
    if (bldms_mounted){
        printk("%s: the device is already mounted and it supports only 1 single mount at a time\n", MOD_NAME);
        return ERR_PTR(-EBUSY);
    }

        
    // pass custom callback function to fill the superblock
    ret = mount_bdev(fs_type, flags, dev_name, data, bldms_fs_fill_super);
    if (unlikely(IS_ERR(ret)))
        printk("%s: error mounting the file system\n", MOD_NAME);
    else{
        // try to save a reference to the superblock of the device, in order to allow system calls to use it
        the_device = blkdev_get_by_path(dev_name, FMODE_READ | FMODE_WRITE, NULL);
        if(IS_ERR(the_device)){
            free_data_structures();
            printk("%s: error getting a reference to the device\n", MOD_NAME);
            return ERR_PTR(-EINVAL);
        }
        the_dev_superblock = the_device->bd_super;

        if(the_dev_superblock && the_dev_superblock->s_bdev)
            printk("%s: got superblock reference - it has magic number 0x%lx", MOD_NAME, the_dev_superblock->s_magic);
        else{
            printk("%s: unable to get a reference to the device superblock\n", MOD_NAME);
            free_data_structures();
            return ERR_PTR(-EINVAL);
        }
    }

    return ret;
}


static struct file_system_type bldms_fs_type = {
    .owner      = THIS_MODULE,
    .name       = BLDMS_FS_NAME,
    .mount      = bldms_fs_mount,
    .kill_sb    = bldms_fs_kill_sb
};

static int __init bldms_init(void){
    int ret;

    // register system calls
    ret = register_syscalls();
    if(unlikely(ret < 0)){
        printk("%s: something went wrong in syscall registration", MOD_NAME);
        return ret;
    }

    // register the filesystem type
    ret = register_filesystem(&bldms_fs_type);
    if (likely(ret == 0))
        printk("%s: successfully registered %s\n", MOD_NAME, bldms_fs_type.name);
    else
        printk("%s: failed to register %s - error %d\n", MOD_NAME, bldms_fs_type.name, ret);

    return ret;
}

static void __exit bldms_exit(void){
    int ret;

    // unregister system calls
    unregister_syscalls();
    
    //unregister filesystem
    ret = unregister_filesystem(&bldms_fs_type);

    if (likely(ret == 0))
        printk("%s: sucessfully unregistered %s driver\n",MOD_NAME, bldms_fs_type.name);
    else
        printk("%s: failed to unregister %s driver - error %d", MOD_NAME, bldms_fs_type.name, ret);
}


module_init(bldms_init);
module_exit(bldms_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Andrea Pepe <pepe.andmj@gmail.com>");
MODULE_DESCRIPTION("BLDMS: Block-Level Data Management Service");
