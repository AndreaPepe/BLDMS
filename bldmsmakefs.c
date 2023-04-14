#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <stdint.h>
#include "include/bldms.h"

/**
 * @brief This makefs will format the BLDMS device writing the following
 * information onto the disk:
 *  - BLOCK 0, superblock;
 *  - BLOCK 1, inode of the unique file (the inode for root is volatile)
 *  - BLOCK 2, ..., N, inodes and datablocks for th messages.
*/

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
    // bldms_block tmp_metadata;

    if (argc != 2){
        printf("Usage: %s <device>\n", argv[0]);
        return -1;
    }

    fd = open(argv[1], O_RDWR);
    if (fd < 0){
        perror("Error opening the device\n");
        return -1;
    }

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

    // write the inode on the device
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
    * Init metadata of blocks:
    * - ndx: 32 bits set to the value of the block in the device
    * - valid_bytes: 32 bits initialized to zero
    * - ts: timestamp struct values for a total of 128 bits initialized to zero
    * - is_valid: 1 byte, initialized to 0 (not valid) for each block
    * */
    num_data_blocks = file_inode.file_size / DEFAULT_BLOCK_SIZE;
    // tmp_metadata.is_valid = BLK_INVALID;
    // tmp_metadata.ts.tv_nsec = 0;
    // tmp_metadata.ts.tv_sec = 0;
    nbytes = DEFAULT_BLOCK_SIZE - 4 - 4 -16 -1;

    // initialized to zero, also used for zero values other than padding
    block_padding = calloc(nbytes, 1);

    for (i=0; i<num_data_blocks; i++){
        // tmp_metadata.ndx = i;

        // write ndx
        ret = write(fd, &i, sizeof(uint32_t));
        if (ret != sizeof(i)){
            printf("Error writing device block's metadata (index)\n");
            close(fd);
            return -1;
        }

        // write valid_bytes + timestamp + is_valid
        ret = write(fd, block_padding, sizeof(uint32_t) + 2*sizeof(uint64_t) + sizeof(unsigned char));
        if (ret != sizeof(uint32_t) + 2*sizeof(uint64_t) + sizeof(unsigned char)){
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