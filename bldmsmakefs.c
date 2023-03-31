#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

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

    if (argc != 2){
        printf("Usage: %s <device>\n", argv[0]);
        return -1;
    }

    fd = open(argv[1], O_RDWR);
    if (fd < 0){
        perror("Error opening the device\n");
        return -1;
    }

    // pack the superblock
    sb_info.version = 1;
    sb_info.magic = MAGIC;
    sb_info.block_size = DEFAULT_BLOCK_SIZE;
    sb_info.block_count = NBLOCKS;

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
    // empty file at the beginning
    file_inode.file_size = 0;

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
       
    // all other blocks are reserved for user datablocks

    close(fd);
    return 0;
}