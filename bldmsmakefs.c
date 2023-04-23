/**
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
 *
 * @file bldmsmakefs.c - file system formatter for the Block-Level Data Management System (BLDMS)
 * @brief file system formatter for the Block-Level Data Management System (BLDMS).
 *        If compiled with the FILL_DEV directive, the device is initially formatted with some valid messages
 *        pre-installed.
 * @author Andrea Pepe
 * @date April 22, 2023  
*/

#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "include/bldms.h"

/**
 * @brief This makefs will format the BLDMS device writing the following
 * information onto the disk:
 *  - BLOCK 0, superblock;
 *  - BLOCK 1, inode of the unique file (the inode for root is volatile)
 *  - BLOCK 2, ..., N, inodes and datablocks for th messages.
*/

#define BILLION 1000000000L

typedef struct __attribute__((packed)) _blk{
    uint64_t nsec;
    unsigned char is_valid : 1;
    uint16_t valid_bytes : 15;
} blk;

#define BLK_MD_SIZE sizeof(blk)

int main(int argc, char **argv){
    int fd, nbytes;
    ssize_t ret;
    struct bldms_sb_info sb_info;
    struct bldms_inode file_inode;
    struct bldms_dir_record dir_record;
    char *block_padding;
    struct stat st;
    off_t size;
    uint num_data_blocks;
    uint32_t i;

    if (argc != 2){
        printf("Usage: %s <image>\n", argv[0]);
        return -1;
    }

    fd = open(argv[1], O_RDWR);
    if (fd < 0){
        perror("Error opening the device\n");
        return -1;
    }

    // get the size of the passed image file
    fstat(fd, &st);
    size = st.st_size;

    // pack the superblock
    sb_info.version = 1;
    sb_info.magic = MAGIC;

    // write on the device
    ret = write(fd, (char *)&sb_info, sizeof(sb_info));

    if(ret != DEFAULT_BLOCK_SIZE){
        printf("Written bytes [%d] are not equal to the default block size.\n", (int)ret);
        close(fd);
        return ret;
    }

    printf("Superblock written successfully\n");

    // write single file inode
    file_inode.mode = S_IFREG;
    file_inode.inode_no = BLDMS_SINGLEFILE_INODE_NUMBER;
    // device size is the size of the image file minus the size of the superblock and of the device file inode block
    file_inode.file_size = size - (2 * DEFAULT_BLOCK_SIZE);
    printf("Detected file size is: %ld\n", file_inode.file_size);

    // write the inode of the device (i.e. of the single file)
    ret = write(fd, (char *)&file_inode, sizeof(file_inode));
    if (ret != sizeof(file_inode)){
        printf("The file inode was not properly written.\n");
        close(fd);
        return -1;
    }

    printf("File inode successfully written\n");

    // padding for the block containing the file inode
    nbytes = DEFAULT_BLOCK_SIZE - ret;
    block_padding = malloc(nbytes);
    ret = write(fd, block_padding, nbytes);
    if (ret != nbytes){
        printf("Padding for file inode block was not properly written.\n");
        close(fd);
        return -1;
    }
    printf("Padding for the block containing the file inode succesfully written.\n");


    // all other blocks are reserved for user datablocks but the metadata needs to be written

    /*
    * Initialize metadata of each block of the block device:
    * - nsec: 8 bytes timestamp value, initialized to zero
    * - is_valid: 1 bit, initialized to 0 (not valid) for each invalid block, to 1 for the valid ones
    * - valid_bytes: 15 bits, initialized to 0 for invalid blocks
    * */
    num_data_blocks = file_inode.file_size / DEFAULT_BLOCK_SIZE;
    blk my_blk = {
        .nsec = 0,
        .is_valid = BLK_INVALID,
        .valid_bytes = 0
    };

    nbytes = DEFAULT_BLOCK_SIZE - BLK_MD_SIZE;
    // initialized to zero, also used for zero values other than padding
    block_padding = calloc(nbytes, 1);
    char *string0 = "This is the message present at the first block, but with a timestamp of 100 seconds greater than the original\n";
    char *string5 = "Hello, I am a message present in block number 5!\n";
    char *string9 = "This is message for block 9, with timestamp of 9 seconds greater than it should be :)\n";
    char *string17 = "Hi there, this is message from block number 17 and my timestamp has been increased exactly of 17 seconds ;)\n";
    char *string22 = "I'm just a normal message put in block 22, but at least by block number is palindrome :)\n";
    for (i=0; i<num_data_blocks; i++){
#ifdef FILL_DEV
        if (i==0 || i==5 || i==9 || i==17 || i==22){
            char *s;
            switch(i){
                case 0:
                    s = string0; break;
                case 5:
                    s = string5; break;
                case 9:
                    s = string9; break;
                case 17:
                    s = string17; break;
                case 22:
                    s = string22; break;
            }

            uint16_t valid_bytes = strlen(s) + 1;       //take into account also the string terminator character
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            
            // tv_nsec are the expired nsec in the second specified by tv_sec: bring all to nsec count
            signed long long nsec = ts.tv_sec*BILLION + ts.tv_nsec;
            if (i == 9 || i== 17){
                nsec += i*BILLION;                      // add seconds equal to the block number to make timestamp order differ from index order
            }else if (i == 0){
                nsec += 100*BILLION;                    // add 100 seconds to the block in the first position on the device, in order to give it the biggest timestamp
            }

            my_blk.nsec = nsec;
            my_blk.is_valid = BLK_VALID;
            my_blk.valid_bytes = valid_bytes;
            ret = write(fd, &my_blk, BLK_MD_SIZE);
            if(ret != BLK_MD_SIZE){
                printf("Error initializing device block content: ret is %ld, should have been %d\n", ret, nbytes - valid_bytes);
                close(fd);
                return -1;
            }

            ret = write(fd, s, my_blk.valid_bytes);
            if(ret != my_blk.valid_bytes){
                printf("Error initializing device block content: ret is %ld, should have been %d\n", ret, nbytes - valid_bytes);
                close(fd);
                return -1;
            }

            ret = write(fd, block_padding, nbytes - my_blk.valid_bytes);
            if(ret != nbytes - my_blk.valid_bytes){
                printf("Error initializing device block padding: ret is %ld, should have been %d\n", ret, nbytes - valid_bytes);
                close(fd);
                return -1;
            }

            continue;
        }
#endif
        my_blk.nsec = 0;
        my_blk.is_valid = BLK_INVALID;
        my_blk.valid_bytes = 0;
        ret = write(fd, &my_blk, BLK_MD_SIZE);
        if (ret != BLK_MD_SIZE){
            printf("Error writing device block's metadata (fields set to 0)\n");
            close(fd);
            return -1;
        }

        // write block data: it is initialized to 0
        ret = write(fd, block_padding, nbytes);
        if(ret != nbytes){
            printf("Error initializing device block content\n");
            close(fd);
            return -1;
        }
    }

    printf("File system formatted correctly\n");
    close(fd);
    return 0;
}