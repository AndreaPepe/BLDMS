#pragma once
#ifndef __RCU_LIST_H__
#define __RCU_LIST_H__


#include <linux/types.h>
#include <linux/list.h>
#include <linux/rculist.h>
#include <linux/spinlock.h>
#include "device.h"

static LIST_HEAD(valid_blk_list);
static spinlock_t rcu_write_lock;

typedef struct _rcu_elem {
    uint32_t ndx;
    size_t valid_bytes;
    struct list_head node;
}rcu_elem;


/* functions*/

extern int add_valid_block(bldms_block *metadata);
extern int remove_valid_block(uint32_t ndx);

#endif