#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/syscalls.h>
#include <linux/blkdev.h>
#include <linux/buffer_head.h>

#include "lib/include/usctm.h"  
#include "include/bldms.h"
#include "include/rcu.h"
#include "include/syscalls.h"

unsigned long the_syscall_table = 0x0;

unsigned long the_ni_syscall;

unsigned long new_sys_call_array[] = {0x0, 0x0, 0x0};
#define HACKED_ENTRIES (int)(sizeof(new_sys_call_array)/sizeof(unsigned long))
int restore_entries[HACKED_ENTRIES] = {[0 ... (HACKED_ENTRIES-1)] -1};
int indexes[HACKED_ENTRIES] = {[0 ... (HACKED_ENTRIES-1)] -1};

/* 
 * put_data() system call
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 17, 0)
__SYSCALL_DEFINEx(2, _put_data, char *, source, size_t, size){
#else
asmlinkage int sys_put_data(char *source, size_t size){
#endif  
    int i;
    unsigned long ret;
    uint32_t target_block;
    struct block_device *the_device;
    struct super_block *sb;
    struct buffer_head *bh;

    bldms_block *old_metadata, *new_metadata;
    char *buffer;
    char *backup; 

    // if the device is not mounted, return the ENODEV error
    if(!bldms_mounted)
        return -ENODEV;

    if(size > DEFAULT_BLOCK_SIZE - METADATA_SIZE){
        // the message is too big and can not be kept in a single block
        return -E2BIG;
    }

    ret = copy_from_user(buffer + METADATA_SIZE, source, size);
    if (ret != 0){
        printk("%s: copy_from_user() unable to read the full message\n", MOD_NAME);
        return -EMSGSIZE;
    }

    if(!the_device_name){
        printk("%s: the device name is NULL\n", MOD_NAME);
        return -1;
    }

    the_device = blkdev_get_by_path(the_device_name, FMODE_WRITE | FMODE_READ, NULL);
    sb = the_device->bd_super;

    /* getting the write lock here: this way, we can be sure can the metadata array is not accesed by anyone else in the meanwhile*/
    spin_lock(&rcu_write_lock);
    target_block = -1;
    for(i=last_written_block + 1; i != last_written_block; i = ((i+1)% md_array_size)){
        /*
        * The next free block to perform the valid operation is chosen 
        * in a circular buffer manner, starting from the block following the last written one.
        */
        if (metadata_array[i]->is_valid == BLK_INVALID){
            // this is the target block
            target_block = i;
            break;
        }
    }

    if (target_block < 0){
        // no available free blocks
        blkdev_put(the_device, FMODE_WRITE | FMODE_READ);
        spin_unlock(&rcu_write_lock);
        return -ENOMEM;
    }

    buffer = kzalloc(DEFAULT_BLOCK_SIZE, GFP_KERNEL);
    if(!new_metadata){
        blkdev_put(the_device, FMODE_WRITE | FMODE_READ);
        spin_unlock(&rcu_write_lock);
        return -EADDRNOTAVAIL;
    }

    backup = kzalloc(DEFAULT_BLOCK_SIZE, GFP_KERNEL);
    if(!new_metadata){
        kfree(buffer);
        blkdev_put(the_device, FMODE_WRITE | FMODE_READ);
        spin_unlock(&rcu_write_lock);
        return -EADDRNOTAVAIL;
    }

    old_metadata = metadata_array[target_block];
    new_metadata = kzalloc(sizeof(bldms_block), GFP_KERNEL);
    if(!new_metadata){
        kfree(buffer);
        kfree(backup);
        blkdev_put(the_device, FMODE_WRITE | FMODE_READ);
        spin_unlock(&rcu_write_lock);
        return -EADDRNOTAVAIL;
    }

    memcpy(new_metadata, old_metadata, sizeof(bldms_block));
    // get the actual time as creation timestamp for the message
    new_metadata->nsec = ktime_get_real();
    printk("%s: creation timestamp for the message is %lld\n", MOD_NAME, new_metadata->nsec);
    new_metadata->is_valid = BLK_VALID;
    new_metadata->valid_bytes = size;
    // write the block metadata in the in-memory buffer
    memcpy(buffer, (char *)new_metadata, sizeof(bldms_block));

    /*
    * Since the target block is invalid, it surely will never become valid until the write_lock is released.
    * Although, the moment after the RCU element is added to the list, some reader could request the block
    * and read it from the device. So, first make sure to write the block on the device and then update the RCU list.
    */
    bh = sb_bread(sb, target_block + NUM_METADATA_BLKS);
    // save a backup for eventual restore of the block
    memcpy(backup, bh->b_data, bh->b_size);
    memcpy(bh->b_data, buffer, DEFAULT_BLOCK_SIZE);
    bh->b_size = DEFAULT_BLOCK_SIZE;
    mark_buffer_dirty(bh);
    brelse(bh);

#if SYNCHRONOUS_PUT_DATA
    // synchronously flush the changes on the block device
    fsync_bdev(the_device);
#endif

    // add the element to the RCU list
    ret = add_valid_block_secure(new_metadata->ndx, new_metadata->valid_bytes, new_metadata->nsec);
    if(ret < 0){
        // error occurred: restore the value on the device
        memcpy(bh->b_data, backup, size + METADATA_SIZE);
        bh->b_size = DEFAULT_BLOCK_SIZE;
        mark_buffer_dirty(bh);
        brelse(bh);

        kfree(new_metadata);
        kfree(backup);
        kfree(buffer);
        blkdev_put(the_device, FMODE_WRITE | FMODE_READ);
        spin_unlock(&rcu_write_lock);
        return -1;
    }

    // update the metadata structure and the last written block and release the lock to make changes effective
    metadata_array[target_block] = new_metadata;
    last_written_block = target_block;
    spin_unlock(&rcu_write_lock);
    blkdev_put(the_device, FMODE_WRITE | FMODE_READ);
    kfree(backup);
    kfree(buffer);
    kfree(old_metadata);
    return (int)target_block;
}

// get_data() system call
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 17, 0)
__SYSCALL_DEFINEx(3, _get_data, int, offset, char *, destination, size_t, size){
#else
asmlinkage int sys_get_data(int offset, char *destination, size_t size){
#endif
    return 0;
}

// invalidate_data() system call
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 17, 0)
__SYSCALL_DEFINEx(1, _invalidate_data, int, offset){
#else
asmlinkage int sys_invalidate_data(int offset){
#endif
    return 0;
}


#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,17,0)
long sys_put_data = (unsigned long) __x64_sys_put_data;
long sys_get_data = (unsigned long) __x64_sys_get_data;
long sys_invalidate_data = (unsigned long) __x64_sys_invalidate_data;
#else
#endif


int register_syscalls(void){
    int ret, i;

    ret = get_entries(restore_entries, indexes, HACKED_ENTRIES, &the_syscall_table, &the_ni_syscall);
    if(ret != HACKED_ENTRIES){
        printk("%s: unable to register system calls - get_entries failed returning %d\n", MOD_NAME, ret);
        return -1;
    }

    /* the system calls will be installed in this order
     * 1. put_data();
     * 2. get_data();
     * 3. invalidate_data();
     */
    new_sys_call_array[0] = (unsigned long)sys_put_data;
    new_sys_call_array[1] = (unsigned long)sys_get_data;
    new_sys_call_array[2] = (unsigned long)sys_invalidate_data;

    unprotect_memory();
    for(i=0; i<HACKED_ENTRIES; i++){
        ((unsigned long *)the_syscall_table)[restore_entries[i]] = (unsigned long)new_sys_call_array[i];
        printk("%s: system call %d installed with index %d\n", MOD_NAME, i+1, restore_entries[i]);
    }
    protect_memory();

    printk("%s: all new system calls correctly installed on system-call table\n", MOD_NAME);
    return 0;
}


void unregister_syscalls(void){
    int i;

    unprotect_memory();
    for (i=0; i<HACKED_ENTRIES; i++){
        ((unsigned long *)the_syscall_table)[restore_entries[i]] = the_ni_syscall;
    }
    protect_memory();

    reset_entries(restore_entries, indexes, HACKED_ENTRIES);

    printk("%s: sys-call table restored to its original content\n", MOD_NAME);
}