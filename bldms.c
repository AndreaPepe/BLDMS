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


unsigned char bldms_mounted = 0;
bldms_block **metadata_array;
size_t md_array_size;

/**
 * @brief  Returns 0 if they are equal, > 0 if lts is after rts, < 0 otherwise.
 */
static inline long timespec64_comp(struct timespec64 lts, struct timespec64 rts){
    return timespec64_sub(lts, rts).tv_nsec;
}

static inline long ktime_comp(ktime_t lkt, ktime_t rkt){
    return lkt - rkt;
}

void merge(bldms_block **arr, int l, int m, int r)
{
    int i, j, k;
    int n1 = m - l + 1;
    int n2 = r - m;
    long comparison;
 
    /* create temp arrays */
    bldms_block **L, **R;
    L = kzalloc(sizeof(bldms_block *) * n1, GFP_KERNEL);
    R = kzalloc(sizeof(bldms_block *) * n2, GFP_KERNEL);
    /* Copy data to temp arrays L[] and R[] */
    for (i = 0; i < n1; i++)
        L[i] = arr[l + i];
    for (j = 0; j < n2; j++)
        R[j] = arr[m + 1 + j];
 
    /* Merge the temp arrays back into arr[l..r]*/
    i = 0; // Initial index of first subarray
    j = 0; // Initial index of second subarray
    k = l; // Initial index of merged subarray
    while (i < n1 && j < n2) {
        //comparison = timespec64_comp(L[i]->ts, R[j]->ts);
        comparison = ktime_comp(L[i]->nsec, R[j]->nsec);
        if (comparison < 0 || (comparison == 0 && L[i]->ndx < R[j]->ndx)) {
            // if the timestamp is the same, order by index of the block
            // left timestamp is before right timestamp
            arr[k] = L[i];
            i++;
        }
        else{
            arr[k] = R[j];
            j++;
        }
        k++;
    }
 
    /* Copy the remaining elements of L[], if there
    are any */
    while (i < n1) {
        arr[k] = L[i];
        i++;
        k++;
    }
 
    /* Copy the remaining elements of R[], if there
    are any */
    while (j < n2) {
        arr[k] = R[j];
        j++;
        k++;
    }
    kfree(L);
    kfree(R);
}
 
/* l is for left index and r is right index of the
sub-array of arr to be sorted */
void merge_sort(bldms_block **arr, int l, int r)
{
    if (l < r) {
        // Same as (l+r)/2, but avoids overflow for
        // large l and h
        int m = l + (r - l) / 2;
 
        // Sort first and second halves
        merge_sort(arr, l, m);
        merge_sort(arr, m + 1, r);
 
        merge(arr, l, m, r);
    }
}

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
    int i, nr_valid_blocks, ret;
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

    // file-system specific data
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
    brelse(bh);
    if (the_file_inode->file_size > NBLOCKS * DEFAULT_BLOCK_SIZE){
        // unamangeable block: too big
        return -E2BIG;
    }

    // compute the number of blocks of the device and init an array of blocks metadata
    md_array_size = the_file_inode->file_size / DEFAULT_BLOCK_SIZE;
    metadata_array = kzalloc(sizeof(bldms_block *) * md_array_size, GFP_KERNEL);
    if(! metadata_array){
        return -ENOMEM;
    }
    pr_info("%s: the device has %lu blocks\n", MOD_NAME, md_array_size);

    // this is a temp array used to order blocks by ascending timestamp, in order to place them in order in the RCU list
    tmp_array = kzalloc(sizeof(bldms_block *) * md_array_size, GFP_KERNEL);
    nr_valid_blocks = 0;
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
        brelse(bh);

        // if it's a valid block, also insert it into the initial RCU list
        if (metadata_array[i]->is_valid == BLK_VALID){
            pr_info("%s: Block of index %u is valid - it has timestamp %lld\n", MOD_NAME, metadata_array[i]->ndx, metadata_array[i]->nsec);
            // insert the metadata block also in the temp array and keep track of the number of valid blocks
            tmp_array[nr_valid_blocks] = metadata_array[i];
            nr_valid_blocks++;
        }
    }

    //sort the array of valid blocks comparing timestamps
    merge_sort(tmp_array, 0 , nr_valid_blocks - 1);
    kfree(tmp_array);

    /*
    * Initialize the RCU list of valid blocks, by pushing in order the blocks
    * already present and valid found on the device.
    * The RCU list will always be kept in timestamp order, since new additions
    * will happen only on write operations with a timestamp greater of the timestamps
    * of the previous valid blocks: so, push to the tail of the RCU list is enough. 
    */
    rcu_init();
    for(i=0; i<nr_valid_blocks; i++){
        ret = add_valid_block(tmp_array[i]->ndx, tmp_array[i]->valid_bytes, tmp_array[i]->nsec);
        if (ret < 0){
            return ret;
        }
    }

    //TEST
    list_for_each_entry_rcu(rcu_el, &valid_blk_list, node){
        pr_info("%s: RCU elem has index - %d\n", MOD_NAME, rcu_el->ndx);
    }
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
