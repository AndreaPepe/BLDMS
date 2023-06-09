#pragma once
#ifndef __BLDMS_H_
#define __BLDMS_H_

#include <linux/fs.h>
#include <linux/types.h>

#define MOD_NAME "BLDMS"
#define BLDMS_FS_NAME "bldms_fs"

#define MAGIC 0x30303030
#define DEFAULT_BLOCK_SIZE 4096

#ifndef NBLOCKS
    #define NBLOCKS 1000
#endif

#define SB_BLOCK_NUMBER 0
#define BLDMS_SINGLEFILE_INODE_NUMBER 1
#define ROOT_INODE_NUMBER 2

#define UNIQUE_FILE_NAME "the_file"
#define UNIQUE_FILE_NAME_SIZE 8
#define BLDMS_INODES_BLOCK_NUMBER 1
#define FILENAME_MAX_LEN 255

#define BLK_INVALID (0)
#define BLK_VALID (BLK_INVALID + 1)

#ifndef DEBUG
    #define DEBUG 0
#endif
#define AUDIT if(DEBUG)


extern unsigned char bldms_mounted;
extern struct super_block *the_dev_superblock;



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

    //padding to fit into a single block
    char padding[ DEFAULT_BLOCK_SIZE - (2 * sizeof(uint64_t))];
};


// file_ops.c
extern const struct inode_operations bldms_inode_ops;
extern const struct file_operations bldms_file_operations;

// dir_ops.c
extern const struct file_operations bldms_dir_operations;
#endif

