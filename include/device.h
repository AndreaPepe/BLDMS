#pragma once
#ifndef __BLDMS_DEVICE_H__
#define __BLDMS_DEVICE_H__

#include <linux/ktime.h>
#include <linux/types.h>

// device's block metadata
typedef struct bldms_block{
    uint32_t ndx;                   // 32 bit
    uint32_t valid_bytes;           // 32 bit
    struct timespec64 ts;           // 128 bit   
    unsigned char is_valid;         // 8 bit    
}bldms_block;

#endif