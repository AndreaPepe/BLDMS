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

/**
 * @brief  Adds a node representing a valid block to a RCU list of valid blocks 
 * @param  metadata: pointer to a block metadata structure
 * @retval less than 0 if error, 0 if ok
 */
int add_valid_block(bldms_block *metadata){
    rcu_elem *el;
    el = kzalloc(sizeof(rcu_elem), GFP_KERNEL);
    if (!el)
        return -ENOMEM;

    el->ndx = metadata->ndx;
    el->valid_bytes = metadata->valid_bytes;

    spin_lock(&rcu_write_lock);
    list_add_tail_rcu(&el->node, &valid_blk_list);
    spin_unlock(&rcu_write_lock);
    return 0;    
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