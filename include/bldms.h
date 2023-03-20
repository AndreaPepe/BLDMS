#pragma once
#ifndef __BLDMS_H_
#define __BLDMS_H_

#include <linux/fs.h>

#define MOD_NAME "BLDMS"
#define MAGIC 0x30303030
#define DEFAULT_BLOCK_SIZE 4096
#define SB_BLOCK_NUMBER 0
#define FILENAME_MAX_LEN 255
#define ROOT_INODE_NUMBER 2


//inode definition
struct bldms_inode {
    mode_t mode;                                    //not exploited
    uint64_t inode_no;
    uint64_t data_block_number;                     //not exploited

    union {
        uint64_t file_size;
        uint64_t dir_children_count;
    };
};

//dir definition (how the dir data block is organized)
struct bldms_dir_record {
    char filename[FILENAME_MAX_LEN];
    uint64_t inode_no;
};

//super-block definition
struct bldms_sb_info {
    uint64_t version;
    uint64_t magic;
    uint64_t block_size;
    uint64_t inodes_count;
    uint64_t free_blocks;

    //padding to fit into a single block
    char padding[ DEFAULT_BLOCK_SIZE - (5 * sizeof(uint64_t))];
};


// file.c
extern const struct inode_operations bldms_inode_ops;
extern const struct file_operations bldms_file_operations;

// dir.c
extern const struct file_operations bldms_dir_operations;
#endif

