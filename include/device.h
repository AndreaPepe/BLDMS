#pragma once
#ifndef __BLDMS_DEVICE_H__
#define __BLDMS_DEVICE_H__

#include <linux/ktime.h>
#include <linux/types.h>

// device's block metadata
typedef struct __attribute__((packed)) bldms_block{
    ktime_t nsec;                           // 64 bit
    unsigned char is_valid : 1;             // 1 bit
    uint16_t valid_bytes : 15;              // 15 bit   
}bldms_block;

#define METADATA_SIZE sizeof(bldms_block)   // 10 bytes
#define NUM_METADATA_BLKS 2 		        // superblock + unique file inode

extern bldms_block **metadata_array;
extern size_t md_array_size;
extern uint32_t last_written_block;

#endif