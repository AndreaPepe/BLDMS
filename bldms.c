#include <linux/module.h>
#include <linux/printk.h>
#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/buffer_head.h>
#include <linux/version.h>
#include <linux/timekeeping.h>
#include <linux/string.h>
#include <linux/blkdev.h>

#include "include/bldms.h"
#include "include/rcu.h"
#include "include/syscalls.h"


unsigned char bldms_mounted = 0;
bldms_block **metadata_array;
size_t md_array_size;
char *the_device_name = NULL;
uint32_t last_written_block = 0;

/**
 * @brief  Returns 0 if they are equal, > 0 if lts is after rts, < 0 otherwise.
 */
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
        if (comparison < 0 ) {
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
    int i, last_valid_block, ret;
    int *valid_indexes;
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


    bh = sb_bread(sb, BLDMS_SINGLEFILE_INODE_NUMBER);
    if(!bh){
        return -EIO;
    }
    
    /*
    * Check on the maximum number of manageable blocks
    * Since the device is actually a file, the FS-mount operation corresponds to the device-mount operation 
    * */
    the_file_inode = (struct bldms_inode *) bh->b_data;
    
    if (the_file_inode->file_size > NBLOCKS * DEFAULT_BLOCK_SIZE){
        // unamangeable block: too big
        return -E2BIG;
    }
    
    // compute the number of blocks of the device and init an array of blocks metadata
    md_array_size = the_file_inode->file_size / DEFAULT_BLOCK_SIZE;
    brelse(bh);

    pr_info("%s: the device has %lu blocks\n", MOD_NAME, md_array_size);

    // this is a temp array used to order blocks by ascending timestamp, in order to place them in order in the RCU list
    if(sizeof(bldms_block *) * md_array_size > 1024 * PAGE_SIZE){
        // kzalloc can only allocate up to 4MB; if bigger, use vcalloc
        metadata_array = vcalloc(sizeof(bldms_block *), md_array_size);
        if(!metadata_array){
            return -EINVAL;
        }
        // tmp_array = vcalloc(sizeof(bldms_block *), md_array_size);
        // if(!tmp_array){
        //     vfree(metadata_array);
        //     return -EINVAL;
        // }
        // valid_indexes = vcalloc(sizeof(int), md_array_size);
        // if(!valid_indexes){
        //     vfree(tmp_array);
        //     vfree(metadata_array);
        //     return -EINVAL;
        // }
    }else{
        metadata_array = kzalloc(sizeof(bldms_block *) * md_array_size, GFP_KERNEL);
        if(!metadata_array){
            return -EINVAL;
        }
        // tmp_array = kzalloc(sizeof(bldms_block *) * md_array_size, GFP_KERNEL);
        // if(!tmp_array){
        //     kfree(metadata_array);
        //     return -EINVAL;
        // }
        // valid_indexes = kzalloc(sizeof(int) * md_array_size, GFP_KERNEL);
        // if(!valid_indexes){
        //     kfree(tmp_array);
        //     kfree(metadata_array);
        //     return -EINVAL;
        // }
    }
    pr_info("%s: metadata array allocated\n", MOD_NAME);
    rcu_init();
    for (i = 0; i < md_array_size; i++){
        bh = sb_bread(sb, i + NUM_METADATA_BLKS);
        metadata_array[i] = kzalloc(sizeof(bldms_block), GFP_KERNEL);
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
            // pr_info("%s: Block of index %u is valid - it has timestamp %lld\n", MOD_NAME, metadata_array[i]->ndx, metadata_array[i]->nsec);
            rcu_el = kzalloc(sizeof(rcu_elem), GFP_KERNEL);
            if(!rcu_el){
                ret = -ENOMEM;
                goto err_and_clean_rcu;
            }
            add_valid_block_in_order_secure(rcu_el, i, metadata_array[i]->valid_bytes, metadata_array[i]->nsec);
            // insert the metadata block also in the temp array and keep track of the number of valid blocks
            // tmp_array[nr_valid_blocks] = metadata_array[i];
            // valid_indexes[nr_valid_blocks] = i;
            // nr_valid_blocks++;
        }
    }

    list_for_each_entry(rcu_el, &valid_blk_list, node){
        printk("%s: RCU elem with index %d is present\n", MOD_NAME, rcu_el->ndx);
    }

    //sort the array of valid blocks comparing timestamps
    // merge_sort(tmp_array, 0 , nr_valid_blocks - 1);

    /*
    * Initialize the RCU list of valid blocks, by pushing in order the blocks
    * already present and valid found on the device.
    * The RCU list will always be kept in timestamp order, since new additions
    * will happen only on write operations with a timestamp greater of the timestamps
    * of the previous valid blocks: so, push to the tail of the RCU list is enough. 
    */
    // rcu_init();
    // for(i=0; i<nr_valid_blocks; i++){
    //     printk("%s: adding block with index %d and ts %lld to the RCU list", MOD_NAME, valid_indexes[i], tmp_array[i]->nsec);
    //     ret = add_valid_block(valid_indexes[i], tmp_array[i]->valid_bytes, tmp_array[i]->nsec);
    //     if (ret < 0){
    //         goto err_and_clean_rcu;
    //     }
    // }

    // the number of the last valid block is saved to be used as a reference for finding the next block to be written
    
    last_written_block = (list_empty(&valid_blk_list)) ? list_last_entry(&valid_blk_list, rcu_elem, node)->ndx : 0;
    // if(sizeof(bldms_block *) * md_array_size > 1024 * PAGE_SIZE){
    //     vfree(tmp_array);
    //     vfree(valid_indexes);
    // }else{
    //     kfree(tmp_array);
    //     kfree(valid_indexes);
    // }

    // signal that the device (with the file system) has been mounted
    bldms_mounted = 1;

    return 0;

err_and_clean_rcu:
    // no need to be RCU-safe here, since no one can actually access the list in initialization phase
    list_for_each_entry(rcu_el, &valid_blk_list, node){
        list_del(&(rcu_el->node));
        kfree(rcu_el);
    }

err_and_clean:
    for(; i >= 0; i--){
        kfree(metadata_array[i]);
    }

    if(sizeof(bldms_block *) * md_array_size > 1024 * PAGE_SIZE){
        // vfree(tmp_array);
        vfree(metadata_array);
        // vfree(valid_indexes);
    }else{
        // kfree(tmp_array);
        kfree(metadata_array);
        // kfree(valid_indexes);
    }

    return ret;
}



static void bldms_fs_kill_sb(struct super_block *sb){
    kill_block_super(sb);
    
    // take the spinlock and release it only when all rcu elements are safely deleted from the list
    spin_lock(&rcu_write_lock);
    remove_all_entries_secure();
    

    if(sizeof(bldms_block *) * md_array_size > 1024 * PAGE_SIZE){
        vfree(metadata_array);
    }else{
        kfree(metadata_array);
    }

    if(the_device_name){
        kfree(the_device_name);
    }
    the_device_name = NULL;
    bldms_mounted = 0;
    printk(KERN_INFO "%s: file system unmount successful\n", MOD_NAME);
    spin_unlock(&rcu_write_lock);
    return;
}

// Called on mount operations
struct dentry *bldms_fs_mount(struct file_system_type *fs_type, int flags, const char *dev_name, void *data){
    struct dentry *ret;
    if (bldms_mounted){
        printk("%s: the device is already mounted and it supports only 1 single mount at a time\n", MOD_NAME);
        return ERR_PTR(-EBUSY);
    }

    the_device_name = kzalloc(strlen(dev_name) + 1, GFP_KERNEL);
    if(!the_device_name){
    printk("%s: unable to allocate memory for storing the device name\n", MOD_NAME);
        return ERR_PTR(-ENOMEM);
    }
    
    // pass custom callback function to fill the superblock
    ret = mount_bdev(fs_type, flags, dev_name, data, bldms_fs_fill_super);
    if (unlikely(IS_ERR(ret)))
        printk("%s: error mounting the file system\n", MOD_NAME);
    else{
        // save the name of the device
        memcpy(the_device_name, dev_name, strlen(dev_name) + 1);
        printk("%s: file system correctly mounted on from device %s\n", MOD_NAME, the_device_name);
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
