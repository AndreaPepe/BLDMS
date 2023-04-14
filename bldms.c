#include <linux/module.h>
#include <linux/printk.h>
#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/buffer_head.h>
#include <linux/version.h>
#include <linux/timekeeping.h>
#include <linux/string.h>

#include "include/bldms.h"
#include "include/rcu.h"


static unsigned char bldms_mounted = 0;
bldms_block **metadata_array;
static size_t md_array_size;

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
    bldms_block **tmp_array;
    int i, nr_valid_blocks;

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

    // file-system specific data
    sb->s_fs_info = NULL;
    sb->s_op = &bldms_fs_super_ops;

    root_inode = iget_locked(sb, 0);
    if (!root_inode){
        return -ENOMEM;
    }

    //i_size_read(root_inode);

    root_inode->i_ino = ROOT_INODE_NUMBER;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 12, 0)
    inode_init_owner(NULL, root_inode, NULL, S_IFDIR);
#else
    inode_init_owner(root_inode, NULL, S_IFDIR);
#endif

    root_inode->i_sb = sb;

    // setup inode and file ops
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


    // check if the number of blocks of the unique file of the FS is at most NBLOCKS
    bh = sb_bread(sb, BLDMS_INODES_BLOCK_NUMBER);
    if(!bh){
        return -EIO;
    }
    

    /*
    * Check on the maximum number of manageable blocks
    * Since the device is actually a file, the FS-mount operation is the same of device-mount operation 
    * */
    the_file_inode = (struct bldms_inode *) bh->b_data;
    if (the_file_inode->file_size > NBLOCKS * DEFAULT_BLOCK_SIZE){
        // unamangeable block: too big
        return -E2BIG;
    }

    md_array_size = the_file_inode->file_size / DEFAULT_BLOCK_SIZE;
    metadata_array = kzalloc(sizeof(bldms_block *) * md_array_size, GFP_KERNEL);
    if(! metadata_array){
        return -ENOMEM;
    }

    // this is a temp array used to order blocks by ascending timestamp, in order to place them in order in the RCU list
    tmp_array = kzalloc(sizeof(bldms_block *) * md_array_size, GFP_KERNEL);
    nr_valid_blocks = 0;
    //int ret;
    for (i = 0; i < md_array_size; i++){
        bh = sb_bread(sb, i + 2);
        if (!bh){
            return -EIO;
        }
        metadata_array[i] = kzalloc(sizeof(bldms_block), GFP_KERNEL);
        if (!metadata_array[i]){
            return -ENOMEM;
        }
        
        memcpy(metadata_array[i], bh->b_data, sizeof(bldms_block));

        // if it's a valid block, also insert it into the initial RCU list
        if (metadata_array[i]->is_valid == BLK_VALID){
            // insert the metadata block also in the temp array and keep track of the number of valid blocks
            tmp_array[nr_valid_blocks] = metadata_array[i];
            nr_valid_blocks++;

            // ret = add_valid_block(metadata_array[i]);
            // if (ret < 0){
            //     return ret;
            // }
        }

        //TODO: sort tmp array based on timestamp


    kfree(tmp_array);
    }


    // TODO: build array of blocks metadata
    // TODO: init rcu_list
    // signal that the device (with the file system) has been mounted
    bldms_mounted = 1;

    return 0;
}



static void bldms_fs_kill_sb(struct super_block *sb){
    kill_block_super(sb);
    bldms_mounted = 0;
    printk(KERN_INFO "%s: file system unmount successful\n", MOD_NAME);
    return;
}

// Called on mount operations
struct dentry *bldms_fs_mount(struct file_system_type *fs_type, int flags, const char *dev_name, void *data){
    struct dentry *ret;

    // pass custom callback function to fill the superblock
    ret = mount_bdev(fs_type, flags, dev_name, data, bldms_fs_fill_super);
    if (unlikely(IS_ERR(ret)))
        printk("%s: error mounting the file system\n", MOD_NAME);
    else
        printk("%s: file system correctly mounted on from device %s\n", MOD_NAME, dev_name);

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
