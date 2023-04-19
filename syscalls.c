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
    int i, ret;
    unsigned long copied;
    uint32_t target_block;
    struct block_device *the_device;
    struct super_block *sb;
    struct buffer_head *bh;

    bldms_block *old_metadata, *new_metadata;
    char *buffer;
    rcu_elem *new_elem; 

    // if the device is not mounted, return the ENODEV error
    if(!bldms_mounted)
        return -ENODEV;

    if(size > DEFAULT_BLOCK_SIZE - METADATA_SIZE){
        // the message is too big and can not be kept in a single block
        return -E2BIG;
    }

    buffer = kzalloc(DEFAULT_BLOCK_SIZE, GFP_KERNEL);
    if(!buffer){
        blkdev_put(the_device, FMODE_WRITE | FMODE_READ);
        return -EADDRNOTAVAIL;
    }

    copied = copy_from_user(buffer + METADATA_SIZE, source, size);
    if (copied != 0){
        printk("%s: copy_from_user() unable to read the full message\n", MOD_NAME);
        return -EMSGSIZE;
    }

    if(!the_device_name){
        printk("%s: the device name is NULL\n", MOD_NAME);
        return -1;
    }
    
    // get a reference to the device
    the_device = blkdev_get_by_path(the_device_name, FMODE_WRITE | FMODE_READ, NULL);
    if (IS_ERR(the_device)){
        printk("%s: blkdev_get_by_path() failed in put_data() system call with errno: %lu\n", MOD_NAME, (unsigned long)the_device);
        return -1;
    }

    // get a reference to the superblock
    if(!the_device){
        printk("%s: the device is NULL\n", MOD_NAME);
        kfree(buffer);
        return -EINVAL;
    }
    printk("%s: Device is not null\n", MOD_NAME);

    sb = the_device->bd_super;
    if(!sb){
        printk("%s: superblock is NULL\n", MOD_NAME);
        kfree(buffer);
        return -EINVAL;
    }

    /*
    * Make all the required allocations before the critical section, in order to make it
    * the shortest as possible with possibly non-blocking calls in it.
    */
    new_elem = kzalloc(sizeof(rcu_elem), GFP_KERNEL);
    if(!new_elem){
        kfree(buffer);
        blkdev_put(the_device, FMODE_WRITE | FMODE_READ);
        return -EADDRNOTAVAIL;
    }

    new_metadata = kzalloc(sizeof(bldms_block), GFP_KERNEL);
    if(!new_metadata){
        kfree(buffer);
        kfree(new_elem);
        blkdev_put(the_device, FMODE_WRITE | FMODE_READ);
        return -EADDRNOTAVAIL;
    }

    /*
    * The following calls are performed also if they are not strictly necessary
    * (e.g.) there are no free blocks where to write. They are here in order to reduce 
    * the size of the critical section and avoid possibly blocking calls.
    * 
    * WARNING: calling ktime_get_real() here could result in an out of (timestamp) order
    * insertion in the RCU list. (This thread could sleep and another one could just take
    * the write spinlock before this one).
    * */
    // memcpy(new_metadata, old_metadata, sizeof(bldms_block));
    // get the actual time as creation timestamp for the message
    new_metadata->nsec = ktime_get_real();
    printk("%s: creation timestamp for the message is %lld\n", MOD_NAME, new_metadata->nsec);
    new_metadata->is_valid = BLK_VALID;
    new_metadata->valid_bytes = size;
    // write the block metadata in the in-memory buffer
    // memcpy(buffer, (char *)new_metadata, sizeof(bldms_block));

    /*
    * BEGINNING OF CRITICAL SECTION
    * 
    * getting the write lock here: this way, 
    * we can be sure can the metadata array is not accesed by anyone else in the meanwhile.
    */
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
        ret = -ENOMEM;
        goto error;
    }

    old_metadata = metadata_array[target_block];
    // TODO: move these operations out of the critical section when the ndx field will be removed from the metadata
    new_metadata->ndx = target_block;
    memcpy(buffer, (char *)new_metadata, sizeof(bldms_block));

    /*
    * Since the target block is invalid, it surely will never become valid until the write_lock is released.
    * Although, the moment after the RCU element is added to the list, some reader could request the block
    * and read it from the device. So, first make sure to write the block on the device and then update the RCU list.
    */
    bh = sb_bread(sb, target_block + NUM_METADATA_BLKS);
    if (!bh){
        ret = -1;
        goto error;
    }
    
    memcpy(bh->b_data, buffer, DEFAULT_BLOCK_SIZE);
    bh->b_size = DEFAULT_BLOCK_SIZE;
    mark_buffer_dirty(bh);
#if SYNCHRONOUS_PUT_DATA
    // synchronously flush the changes on the block device: WARNING -> this could be a blocking call
    sync_dirty_buffer(bh);
#endif
    brelse(bh);

    // add the element to the RCU list, after the block is effectively available on the device
    // to avoid wrong ordering of the RCU list, invoke the in order insertion of the node
    add_valid_block_in_order_secure(new_elem, new_metadata->ndx, new_metadata->valid_bytes, new_metadata->nsec);

    // update the metadata structure and the last written block and release the lock to make changes effective
    metadata_array[target_block] = new_metadata;
    last_written_block = target_block;
    spin_unlock(&rcu_write_lock);

    /* END OF CRITICAL SECTION */
    blkdev_put(the_device, FMODE_WRITE | FMODE_READ);
    kfree(buffer);
    kfree(old_metadata);
    return (int)target_block;

error:
    spin_unlock(&rcu_write_lock);

    printk("%s: error occurred during put_data()\n", MOD_NAME);
    blkdev_put(the_device, FMODE_READ | FMODE_WRITE);
    kfree(new_elem);
    kfree(buffer);
    kfree(old_metadata);
    return ret;
}

/*
* get_data() system call
*
* The parameter "offset" is interpreted as the block index of the device
*/
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 17, 0)
__SYSCALL_DEFINEx(3, _get_data, int, offset, char *, destination, size_t, size){
#else
asmlinkage int sys_get_data(int offset, char *destination, size_t size){
#endif
    int bytes_to_copy;
    unsigned long not_copied;
    uint32_t target_block;
    rcu_elem *rcu_el;
    struct block_device *the_device;
    struct super_block *sb;
    struct buffer_head *bh;

    if(!bldms_mounted){
        return -ENODEV;
    }

    if(offset >= md_array_size){
        // the specified block does not exist in the device
        return -E2BIG;
    }

    target_block = offset + NUM_METADATA_BLKS;

    // get a reference to the device
    the_device = blkdev_get_by_path(the_device_name, FMODE_READ, NULL);
    if (IS_ERR(the_device)){
        printk("%s: blkdev_get_by_path() failed in get_data() system call\n", MOD_NAME);
        return -1;
    }

    // get a reference to the superblock
    sb = the_device->bd_super;

    /* 
    * RCU read-side critical section beginning:
    * scan the rcu list of valid blocks to check if the requested block
    * is actually valid.
    */
    rcu_read_lock();
    list_for_each_entry_rcu(rcu_el, &valid_blk_list, node){
        if(rcu_el->ndx == offset){
            // the block is valid and is found
            bytes_to_copy = rcu_el->valid_bytes;
            break;
        }
    }

    // if no block has been found, return -ENODATA: the requested block does not contain valid data
    if(&(rcu_el->node) == &valid_blk_list){
        rcu_read_unlock();
        blkdev_put(the_device, FMODE_READ);
        printk("%s: get_data() - no valid block with offset %d\n", MOD_NAME, offset);
        return -ENODATA;
    }

    bh = sb_bread(sb, target_block);
    if(!bh){
        rcu_read_unlock();
        blkdev_put(the_device, FMODE_READ);
        return -1;
    }

    // if size is greater then the message's valid bytes, copy only valid bytes
    bytes_to_copy = (size > bytes_to_copy) ? bytes_to_copy : size;
    not_copied = copy_to_user(destination, bh->b_data + METADATA_SIZE, (unsigned long)bytes_to_copy);
    brelse(bh);

    /* 
    * The RCU read-side critical section can't finish before
    * because we have to be sure that the content on the device is consistent.
    * When signaling the end of read-side critical section, an overwrite of data
    * on the device could happen (a waiting writer wants to invalidate the block). 
    */
    rcu_read_unlock();
    blkdev_put(the_device, FMODE_READ);
    return (bytes_to_copy - not_copied);
}


/*
* invalidate_data() system call.
* The "offset" parameter is intended to be the device block index.
*/
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 17, 0)
__SYSCALL_DEFINEx(1, _invalidate_data, int, offset){
#else
asmlinkage int sys_invalidate_data(int offset){
#endif
    uint32_t target_block;
    rcu_elem *rcu_el;
    struct block_device *the_device;
    struct super_block *sb;
    struct buffer_head *bh;

    if(!bldms_mounted){
        return -ENODEV;
    }

    if(offset >= md_array_size){
        // the specified block does not exist in the device
        return -E2BIG;
    }

    target_block = offset + NUM_METADATA_BLKS;

    // get a reference to the device
    the_device = blkdev_get_by_path(the_device_name, FMODE_WRITE | FMODE_READ, NULL);
    if (IS_ERR(the_device)){
        printk("%s: blkdev_get_by_path() failed in get_data() system call\n", MOD_NAME);
        return -1;
    }

    // get a reference to the superblock
    sb = the_device->bd_super;

    /*
    * BEGINNING OF CRITICAL SECTION (RCU write-side)
    */
    spin_lock(&rcu_write_lock);
    list_for_each_entry_rcu(rcu_el, &valid_blk_list, node){
        if(rcu_el->ndx == offset){
            // requested block is valid and must be invalidated
            break;
        }
    }

    // if no block has been found, return -ENODATA error
    if(&(rcu_el->node) == &valid_blk_list){
        // no need for rcu synchronization, since no RCU changes have been made
        spin_unlock(&rcu_write_lock);
        blkdev_put(the_device, FMODE_WRITE | FMODE_READ);
        printk("%s: invalidate_data() - no valid block with offset %d\n", MOD_NAME, offset);
        return -ENODATA;
    }

    // get the block in buffer head to stop in case of error before the node is removed from the RCU list
    bh = sb_bread(sb, target_block);
    if(!bh){
        // no need for rcu synchronization, since no RCU changes have been made
        spin_unlock(&rcu_write_lock);
        blkdev_put(the_device, FMODE_WRITE | FMODE_READ);
        return -1;
    }
    
    /*
    * Remove the block from the RCU list, invalidate the entry of the array metadata,
    * release the lock to make changes effective and wait for grace period end 
    * to rewrite its metadata on the device and free the RCU elem structure.
    */
    list_del_rcu(&(rcu_el->node));
    metadata_array[rcu_el->ndx]->is_valid = BLK_INVALID;
    spin_unlock(&rcu_write_lock);

    // wait for grace period end
    synchronize_rcu();

    // rewrite metadata on the device in order to be consistent
    memcpy(bh->b_data, metadata_array[rcu_el->ndx], METADATA_SIZE);
    mark_buffer_dirty(bh);
#if SYNCHRONOUS_PUT_DATA
    sync_dirty_buffer(bh);
#endif
    brelse(bh);

    // free the rcu elem struct
    kfree(rcu_el);
    blkdev_put(the_device, FMODE_READ | FMODE_WRITE);
    printk("%s: invalidate_data() called with offset %d and executed correclty\n", MOD_NAME, offset);
    // return 0 on success
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
    char *syscall_names[HACKED_ENTRIES] = {"put_data()", "get_data()", "invalidate_data()"};

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
        printk("%s: system call %s installed with index %d\n", MOD_NAME, syscall_names[i], restore_entries[i]);
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