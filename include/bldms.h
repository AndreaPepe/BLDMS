#pragma once
#ifndef __BLDMS_H_
#define __BLDMS_H_

#include <linux/fs.h>
#include <linux/types.h>
#include <stdint.h>


#define MOD_NAME "BLDMS"
#define BLDMS_MAJOR 3030
#define BDEV_NAME "bldmsdev"

#define MAGIC 0x30303030
#define DEFAULT_BLOCK_SIZE 4096

#ifndef NBLOCKS
    #define NBLOCKS 100
#endif

#define SB_BLOCK_NUMBER 0
#define FILENAME_MAX_LEN 255
#define ROOT_INODE_NUMBER 2

#define BLK_VALID 0
#define BLK_INVALID (BLK_VALID + 1)
#define BLK_FREE 0
#define BLK_NOT_FREE (BLK_FREE + 1)

#define UNIQUE_FILE_NAME "the_file"
#define BLDMS_INODES_BLOCK_NUMBER 1
#define BLDMS_SINGLEFILE_INODE_NUMBER 1


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
    uint64_t block_count;
    uint64_t inodes_count;
    uint64_t free_blocks;

    //padding to fit into a single block
    char padding[ DEFAULT_BLOCK_SIZE - (6 * sizeof(uint64_t))];
};

// device's block definition
typedef struct bldms_block{
    unsigned char is_free;
    unsigned char is_valid;
    //struct mutex lock;
    char msg[ DEFAULT_BLOCK_SIZE - 2*sizeof(unsigned char)];
}bldms_block;


// file.c
extern const struct inode_operations bldms_inode_ops;
extern const struct file_operations bldms_file_operations;

// dir.c
extern const struct file_operations bldms_dir_operations;
#endif

