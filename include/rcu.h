#pragma once
#ifndef __RCU_LIST_H__
#define __RCU_LIST_H__


#include <linux/types.h>
#include <linux/list.h>
#include <linux/rculist.h>
#include <linux/spinlock.h>
#include "device.h"

extern struct list_head valid_blk_list;
extern spinlock_t rcu_write_lock;

typedef struct _rcu_elem {
    uint32_t ndx;
    ktime_t nsec;
    size_t valid_bytes;
    struct list_head node;
}rcu_elem;


#define rcu_next_elem(el) \
        list_entry_rcu((el)->node.next, rcu_elem, node)

/* functions*/
extern int add_valid_block(uint32_t ndx, uint32_t valid_bytes, ktime_t nsec);
extern int add_valid_block_secure(uint32_t ndx, uint32_t valid_bytes, ktime_t nsec);
extern int remove_valid_block(uint32_t ndx);
extern inline void rcu_init(void);
#endif