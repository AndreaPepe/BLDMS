#pragma once
#ifndef __BLDMS_DEVICE_H__
#define __BLDMS_DEVICE_H__

#include <linux/ktime.h>
#include <linux/types.h>

// device's block metadata
typedef struct __attribute__((packed)) bldms_block{
    uint32_t ndx;                   // 32 bit
    uint32_t valid_bytes;           // 32 bit
    ktime_t nsec;                   // 64 bit   
    unsigned char is_valid;         // 8 bit    
}bldms_block;

#define METADATA_SIZE sizeof(bldms_block)

extern bldms_block **metadata_array;
extern size_t md_array_size;
#endif