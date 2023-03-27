#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>

#include "include/bldms.h"

/**
 * @brief This makefs will format the BLDMS device writing the following
 * information onto the disk:
 *  - BLOCK 0, superblock;
 *  - BLOCK 1, ..., N, inodes and datablocks for th messages.
*/

int main(int argc, char **argv){
    int fd, nbytes;
    ssize_t ret;
    struct bldms_sb_info sb_info;

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

    // write on the device
    ret = write(fd, (char *)&sb_info, sizeof(sb_info));
    if(ret != DEFAULT_BLOCK_SIZE){
        printf("Written bytes [%d] are not equal to the default block size.\n", (int)ret);
        close(fd);
        return ret;
    }
    printf("Superblock written successfully\n");

    // inode for root is volatile, so it's not written
    
    // all other blocks are reserved for user datablocks and their respective inodes

    close(fd);
    return 0;
}