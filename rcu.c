/**
 * rcu.c - rcu list management for BLDMS service
 * 
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
*/

#include "include/rcu.h"
#include <linux/slab.h>


LIST_HEAD(valid_blk_list);
spinlock_t rcu_write_lock;



/**
 * @brief  Adds a node representing a valid block to a RCU list of valid blocks.
 *         RCU locks are taken inside the function.
 * @retval less than 0 if error, 0 if ok
 */
int add_valid_block(uint32_t ndx, uint32_t valid_bytes, ktime_t nsec){
    rcu_elem *el;
    el = kzalloc(sizeof(rcu_elem), GFP_KERNEL);
    if (!el)
        return -ENOMEM;

    el->ndx = ndx;
    el->valid_bytes = valid_bytes;
    el->nsec = nsec;

    spin_lock(&rcu_write_lock);
    list_add_tail_rcu(&el->node, &valid_blk_list);
    spin_unlock(&rcu_write_lock);
    return 0;    
}

/**
 * @brief  This function must be invoked only with a read lock signaled before;
 *         such read lock should be released after the function returns.
 *         The function expects a pointer to a dynamically allocated 
 *         rcu element structure to fill and insert in the list.  
 */
void inline add_valid_block_secure(rcu_elem *el, uint32_t ndx, uint32_t valid_bytes, ktime_t nsec){
    el->ndx = ndx;
    el->valid_bytes = valid_bytes;
    el->nsec = nsec;

    list_add_tail_rcu(&el->node, &valid_blk_list);
    return;    
}

void inline add_valid_block_in_order_secure(rcu_elem *el, uint32_t ndx, uint32_t valid_bytes, ktime_t nsec){
    rcu_elem *prev;
    struct list_head *next;
    el->ndx = ndx;
    el->valid_bytes = valid_bytes;
    el->nsec = nsec;

    if (&valid_blk_list == (&valid_blk_list)->next){
        // the list is empty: just insert the node
        list_add_tail_rcu(&(el->node), &valid_blk_list);
        return;
    }

    list_for_each_entry_reverse(prev, &valid_blk_list, node){
        if (prev->nsec < el->nsec){
            // this is the first node to have a timestamp lower than the new node
            // insert the new node after this one
            next = prev->node.next;
            __list_add_rcu(&(el->node), &(prev->node), next);
            break;
        }
    }
    return;    
}


int remove_valid_block(uint32_t ndx){
    rcu_elem *el;

    // write lock to find the element to be removed and remove it
    spin_lock(&rcu_write_lock);
    list_for_each_entry(el, &valid_blk_list, node){
        if (el->ndx == ndx){
            // this is the element to be removed
            list_del_rcu(&el->node);

            // TODO: update metadata of the block before relasing the lock
            spin_unlock(&rcu_write_lock);

            // wait for the grace period and then free the removed element
            synchronize_rcu();
            kfree(el);
            return 0;
        }
    }

    spin_unlock(&rcu_write_lock);
    return -ENODATA;
}

void remove_all_entries_secure(void){
    rcu_elem *el;

    // write lock should be taken outside
    list_for_each_entry(el, &valid_blk_list, node){
        // this is the element to be removed
        list_del_rcu(&el->node);

        // wait for the grace period and then free the removed element
        synchronize_rcu();
        kfree(el);
    }
}

inline void rcu_init(void){
    spin_lock_init(&rcu_write_lock);
}