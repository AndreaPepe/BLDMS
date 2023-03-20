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



static struct super_operations bldms_super_ops = {
};


static struct dentry_operations bldms_dentry_ops = {
};



int bldms_fill_super(struct super_block *sb, void *data, int silent) {

    struct inode *root_inode;
    struct buffer_head *bh;
    struct bldms_sb_info *sb_disk;
    struct timespec64 curr_time;
    uint64_t magic;

    if(!sb){
        return -EIO;
    }

    //Unique identifier of the filesystem
    sb->s_magic = MAGIC;

    bh = sb_bread(sb, SB_BLOCK_NUMBER);

    sb_disk = (struct bldms_sb_info *)bh->b_data;
    // read the magic number from the device
    magic = sb_disk->magic;
    brelse(bh);

    //check on the expected magic number
    if(magic != sb->s_magic){
        return -EBADF;
    }

    sb->s_fs_info = NULL;               //FS specific data (the magic number) already reported into the generic super-block
    sb->s_op = &bldms_super_ops;        //set our own operations


    root_inode = iget_locked(sb, 0);    //get a root inode indexed with 0 from cache
    if (!root_inode){
        return -ENOMEM;
    }

    root_inode->i_ino = ROOT_INODE_NUMBER;          //this is actually 2

    //set the root user as owned of the FS root
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 12, 0)
    inode_init_owner(&init_user_ns ,root_inode, NULL, S_IFDIR);
#else
    inode_init_owner(root_inode, NULL, S_IFDIR);
#endif
    root_inode->i_sb = sb;
    root_inode->i_op = &bldms_inode_ops;            //set our inode operations
    root_inode->i_fop = &bldms_dir_operations;      //set our file operations
    //update access permission
    root_inode->i_mode = S_IFDIR | S_IRUSR | S_IRGRP | S_IROTH | S_IWUSR | S_IWGRP | S_IXUSR | S_IXGRP | S_IXOTH;

    //baseline alignment of the FS timestamp to the current time
    ktime_get_real_ts64(&curr_time);
    root_inode->i_atime = root_inode->i_mtime = root_inode->i_ctime = curr_time;

    // no inode from device is needed - the root of our file system is an in memory object
    root_inode->i_private = NULL;

    // create the root dentry from the root inode and link inode info to dentry
    sb->s_root = d_make_root(root_inode);
    if (!sb->s_root)
        return -ENOMEM;

    sb->s_root->d_op = &bldms_dentry_ops;//set our dentry operations

    //unlock the inode to make it usable
    unlock_new_inode(root_inode);

    return 0;
}

static void bldms_kill_super_block(struct super_block *s) {
    kill_block_super(s);
    printk(KERN_INFO "%s: bldms unmount is successful.\n",MOD_NAME);
    return;
}

//called on file system mounting
struct dentry *bldms_mount(struct file_system_type *fs_type, int flags, const char *dev_name, void *data) {

    struct dentry *ret;

    ret = mount_bdev(fs_type, flags, dev_name, data, bldms_fill_super);

    if (unlikely(IS_ERR(ret)))
        printk("%s: error mounting bldms\n",MOD_NAME);
    else
        printk("%s: bldms is successfully mounted on from device %s\n",MOD_NAME,dev_name);

    return ret;
}

//file system structure
static struct file_system_type bldms_type = {
        .owner          = THIS_MODULE,
        .name           = "bldms",
        .mount          = bldms_mount,
        .kill_sb        = bldms_kill_super_block,
};


static int bldms_init(void) {

    int ret;

    //register filesystem
    ret = register_filesystem(&bldms_type);
    if (likely(ret == 0))
        printk("%s: successfully registered bldms\n",MOD_NAME);
    else
        printk("%s: failed to register bldms - error %d\n", MOD_NAME,ret);

    return ret;
}

static void bldms_exit(void) {

    int ret;

    //unregister filesystem
    ret = unregister_filesystem(&bldms_type);

    if (likely(ret == 0))
        printk("%s: successfully unregistered file system driver\n",MOD_NAME);
    else
        printk("%s: failed to unregister bldms driver - error %d\n", MOD_NAME, ret);
}

module_init(bldms_init);
module_exit(bldms_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Andrea Pepe <pepe.andmj@gmail.com>");
MODULE_DESCRIPTION("BLDMS: Block-Level Data Management Service");
