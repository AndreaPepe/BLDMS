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
 * @file syscalls.c
 * @brief system calls implementation for the BLDMS block device driver
 *  
 * @author Andrea Pepe
 * 
 * @date April 22, 2023  
*/

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

/**
 * @brief  put_data() system call - add a message in a free block of the BLDMS device
 * @retval The index of the block where the message has been put. Negative number on error;
 * if errno is ENOMEM, it means that there are not free blocks where to write.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 17, 0)
__SYSCALL_DEFINEx(2, _put_data, char *, source, size_t, size){
#else
asmlinkage int sys_put_data(char *source, size_t size){
#endif  
    int i, ret, curr_blk;
    unsigned long copied;
    int target_block;
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

    // get a reference to the superblock
    sb = the_dev_superblock;
    if(!sb){
        return -EINVAL;
    }

    buffer = kzalloc(DEFAULT_BLOCK_SIZE, GFP_ATOMIC);
    if(!buffer){
        return -EADDRNOTAVAIL;
    }

    // copy the message from user space in an intermediate kernel-level buffer
    copied = copy_from_user(buffer + METADATA_SIZE, source, size);
    if (copied != 0){
        kfree(buffer);
        printk("%s: copy_from_user() unable to read the full message\n", MOD_NAME);
        return -EMSGSIZE;
    }

    /*
    * Make all the required allocations before the critical section, in order to make it
    * the shortest as possible; furthermore, this reduces the presence of eventual blocking calls in the CS.
    */
    new_elem = kzalloc(sizeof(rcu_elem), GFP_ATOMIC);
    if(!new_elem){
        kfree(buffer);
        return -EADDRNOTAVAIL;
    }

    new_metadata = kzalloc(sizeof(bldms_block), GFP_ATOMIC);
    if(!new_metadata){
        kfree(buffer);
        kfree(new_elem);
        return -EADDRNOTAVAIL;
    }

    /*
    * The following calls are always performed, also if they could be not necessary:
    * e.g. there are no free blocks where to write. They are anticipated here in order to reduce 
    * the size of the critical section and to avoid the introduction of blocking calls in the CS.
    * 
    * WARNING: calling ktime_get_real() here could result in an out of (timestamp) order
    * insertion in the RCU list. (This thread could sleep after the function returned and another thread, 
    * with a bigger timestamp, could just take the writing spinlock before this one). 
    * Adding the new node to the tail of the RCU list is not safe and an in-order insertion is required
    * to guarantee the ordering of the list.
    */
    old_metadata = NULL;
    // get the actual time as creation timestamp for the message
    new_metadata->nsec = ktime_get_real();
    printk("%s: put_data() - creation timestamp for the new message is %lld\n", MOD_NAME, new_metadata->nsec);
    new_metadata->valid_bytes = size;
    new_metadata->is_valid = BLK_VALID;
    // write the block metadata in the in-memory buffer
    memcpy(buffer, (char *)new_metadata, sizeof(bldms_block));

    /*
    * BEGINNING OF CRITICAL SECTION
    * 
    * getting the write lock here: this way, 
    * we can be sure that the metadata array is not accesed by anyone else in the meanwhile.
    */
    spin_lock(&rcu_write_lock);
    target_block = -1;
    for(i = 1; i <= md_array_size; i++){
        /*
        * The next free block to perform the valid operation is chosen 
        * in a circular buffer manner, starting from the block following the last written one.
        */
        curr_blk = (last_written_block + i) % md_array_size;
        if (metadata_array[curr_blk]->is_valid == BLK_INVALID){
            // this is the target block
            target_block = curr_blk;
            break;
        }
    }

    if (target_block < 0){
        // no available free blocks
        ret = -ENOMEM;
        goto error;
    }

    old_metadata = metadata_array[target_block];

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
    // synchronously flush the changes on the block device: this is a blocking call that can increase the duration of the CS
    sync_dirty_buffer(bh);
#endif
    brelse(bh);

    // add the element to the RCU list, after the block is effectively available on the device
    // to avoid wrong ordering of the RCU list, invoke the in order insertion of the node
    add_valid_block_in_order_secure(new_elem, target_block , new_metadata->valid_bytes, new_metadata->nsec);

    // update the metadata structure and the last written block and release the lock to make changes effective
    metadata_array[target_block] = new_metadata;
    last_written_block = target_block;
    spin_unlock(&rcu_write_lock);

    /* END OF CRITICAL SECTION */
    kfree(buffer);
    kfree(old_metadata);
    return (int)target_block;

error:
    spin_unlock(&rcu_write_lock);

    kfree(new_elem);
    kfree(buffer);
    kfree(new_metadata);
    if(old_metadata != NULL)
        kfree(old_metadata);

    printk("%s: error occurred during put_data()\n", MOD_NAME);
    return ret;
}

/**
 * @brief  get_data() system call - get the content of a block if it is valid
 * In case the requested block is invalid, errno is set to ENODATA.
 * 
 * The parameter "offset" is intended as the number of the block of the device
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

    // get a reference to the device superblock
    sb = the_dev_superblock;
    if(!sb){
        return -EINVAL;
    }

    /* 
    * RCU read-side critical section beginning:
    * scan the RCU list of valid blocks to check if the requested block
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
        printk("%s: get_data() - no valid block with offset %d\n", MOD_NAME, offset);
        return -ENODATA;
    }

    bh = sb_bread(sb, target_block);
    if(!bh){
        rcu_read_unlock();
        return -1;
    }

    // if size is greater then the message's valid bytes, copy only valid bytes
    bytes_to_copy = (size > bytes_to_copy) ? bytes_to_copy : size;
    // write the read data into the specified user-space buffer
    not_copied = copy_to_user(destination, bh->b_data + METADATA_SIZE, (unsigned long)bytes_to_copy);
    brelse(bh);

    /* 
    * The RCU read-side critical section can't finish before this point,
    * because we have to be sure that the content on the device is consistent with respect to
    * what blocks are considered valid, i.e. present in the RCU list.
    * Just after signaling the end of the read-side critical section, an overwrite of data
    * on the device could happen (a waiting writer wants to invalidate the block). 
    */
    rcu_read_unlock();
    return (bytes_to_copy - not_copied);
}


/**
 * @brief  invalidate_data() system call - mark a valid block of the device as logically invalid.
 * If no valid block with the specified offset is found, errno will be set to ENODATA.
 * 
 * The "offset" parameter is intended as the index of the target device's block.
 * The invalidation is only logical: only the validity bit of the block will be affected; the previous content
 * of the block is untouched and remains on the device.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 17, 0)
__SYSCALL_DEFINEx(1, _invalidate_data, int, offset){
#else
asmlinkage int sys_invalidate_data(int offset){
#endif
    uint32_t target_block;
    rcu_elem *rcu_el;
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

    // get a reference to the superblock
    sb = the_dev_superblock;

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
        printk("%s: invalidate_data() - no valid block with offset %d\n", MOD_NAME, offset);
        return -ENODATA;
    }

    // get the block in buffer head to stop in case of error before the node is removed from the RCU list
    bh = sb_bread(sb, target_block);
    if(!bh){
        // no need for rcu synchronization, since no RCU changes have been made
        spin_unlock(&rcu_write_lock);
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
    printk("%s: invalidate_data() on block %d has been executed correctly\n", MOD_NAME, offset);
    // return 0 on success
    return 0;
}





#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,17,0)
long sys_put_data = (unsigned long) __x64_sys_put_data;
long sys_get_data = (unsigned long) __x64_sys_get_data;
long sys_invalidate_data = (unsigned long) __x64_sys_invalidate_data;
#else
#endif

/**
 * @brief  Register the above system calls in the system call table.
 */
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
        printk("%s: system call %s installed; it is associated with code %d\n", MOD_NAME, syscall_names[i], restore_entries[i]);
    }
    protect_memory();

    printk("%s: all new system calls correctly installed on system-call table\n", MOD_NAME);
    return 0;
}

/**
 * @brief  Unregister the above system calls and restore the original content of the
 * system call table.
 */
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