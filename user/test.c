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
 * @file test.c
 * @brief Basic testing program for the BLDMS block device driver.
 * 
 * @author Andrea Pepe
 * @date April 22, 2023  
*/

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include "include/pretty-print.h"
#include "include/quotes.h"

#define BLOCK_SIZE (1<<12)
#define METADATA_SIZE (sizeof(signed long long) + sizeof(uint16_t))
#define MAX_MSG_SIZE (BLOCK_SIZE - METADATA_SIZE)

long put_data_nr = 0x0;
long get_data_nr = 0x0;
long invalidate_data_nr = 0x0;
char *device_filepath;
size_t num_blocks = 0;

// declaration of macros for calling the system calls
#define put_data(source, size) \
            syscall(put_data_nr, source, size)

#define get_data(offset, destination, size) \
            syscall(get_data_nr, offset, destination, size)

#define invalidate_data(offset) \
            syscall(invalidate_data_nr, offset)


int main(int argc, char **argv){
    int i, fd, ret;
    struct stat st;
    char *msg;

    if(argc < 5){
        printf("Usage:\n\t./%s <device file path> <put_data() NR> <get_data() NR> <invalidate_data() NR>\n\n", argv[0]);
        exit(1);
    }

    print_color_bold(YELLOW);
    printf("Initializing ...\n");
    reset_color();

    // save device file location and system call numbers
    device_filepath = argv[1];
    put_data_nr = atol(argv[2]);
    get_data_nr = atol(argv[3]);
    invalidate_data_nr = atol(argv[4]);

    fd = open(device_filepath, O_RDWR);
    if (fd < 0){
        print_color_bold(RED);
        printf("Unable to call open on the specified path %s\n", device_filepath);
        reset_color();
        exit(1);
    }

    fstat(fd, &st);
    num_blocks = st.st_size / BLOCK_SIZE;

    // invalidate all blocks in order to make the device empty
    for (i=0; i < num_blocks; i++){
        ret = invalidate_data(i);
        if (ret < 0){
            if (errno != ENODATA){
                // this error shouldn't happen
                print_color_bold(RED);
                printf("Unable to cleanup the device before testing it\n");
                reset_color();
                exit(1);
            }
        }
    }

    print_color_bold(YELLOW);
    printf("All the messages on the device have been correctly invalidated.\n");
    reset_color();

    // fill the device with messages and check that another put_data result in an error with errno set to ENOMEM
    for (i=0; i < num_blocks; i++){
        msg = messages[i % NUM_MESSAGES];
        ret = put_data(msg, strlen(msg) + 1);
        if(ret < 0){
            // this error shouldn't happen
            print_color_bold(RED);
            printf("put_data() called to fill the device is unexpectedly unsuccessful\n");
            reset_color();
            exit(1);
        }
    }

    print_color_bold(YELLOW);
    printf("The device has been filled with messages. Trying to add another message ...\n");
    reset_color();

    msg = messages[10 % NUM_MESSAGES];
    ret = put_data(msg, strlen(msg) + 1);
    if (!((ret < 0) && (errno == ENOMEM))){
        // this error shouldn't happen
        print_color_bold(RED);
        printf("\nENOMEM was expected, but put_data() succeded or returned a differnt kind of error\n");
        reset_color();
        exit(1);
    }

    print_color(GREEN);
    printf("put_data() set errno to ENOMEM as expected.\n");
    reset_color();

    // invalidate the last block in order to create space for another message
    print_color_bold(YELLOW);
    printf("\nInvalidating the last block of the device, in order to create space for a new message ...\n");
    reset_color();
    ret = invalidate_data(num_blocks - 1);
    if(ret < 0){
        print_color_bold(RED);
        printf("Invalidation of the last block failed\n");
        reset_color();
        exit(1);
    }

    print_color_bold(YELLOW);
    printf("Trying to insert a message bigger than the maximum allowed size ...\n");
    reset_color();
    // try to insert a message larger than the space reserved for a block content - it should return with an error
    char buffer[BLOCK_SIZE] = {[0 ... (BLOCK_SIZE - 2)] 'A', '\0'};
    ret = put_data(buffer, strlen(buffer) + 1);
    if(!((ret < 0) && (errno == E2BIG))){
        // this error shouldn't happen
        print_color_bold(RED);
        printf("\nE2BIG was expected, but put_data() succeded or returned a differnt kind of error\n");
        reset_color();
        exit(1);
    }

    print_color(GREEN);
    printf("put_data() set errno to E2BIG as expected.\n");
    reset_color();

    // try to insert a shorter message - this should return the number of the previously invalidated block
    print_color_bold(YELLOW);
    printf("\nTrying to insert a shorter message - it should return the index of the last block of the device ...\n");
    reset_color();
    for (i=0; i < 50; i++){
        buffer[i] = 'B';
    }
    buffer[50] = '\0';

    ret = put_data(buffer, strlen(buffer) + 1);
    if(ret != (num_blocks - 1)){
        // this error shouldn't happen
        print_color_bold(RED);
        printf("\nput_data() was expected to return the index of the last block of the device (%ld), but returned something else (%d)\n", num_blocks - 1, ret);
        reset_color();
        exit(1);
    }

    print_color(GREEN);
    printf("put_data() returned the device's last block index, as expected.\n");
    reset_color();

    // let's now try to read that message
    print_color_bold(YELLOW);
    printf("\nTrying to read the inserted message ...\n");
    reset_color();
    ret = get_data(ret, buffer, MAX_MSG_SIZE);
    if(ret != 51){
        // this error shouldn't happen
        print_color_bold(RED);
        printf("\nget_data() was expected to return 51, the number of bytes of the message, but returned %d\n", ret);
        reset_color();
        exit(1);
    }

    print_color(GREEN);
    printf("get_data() correctly read the following message: %s\n", buffer);
    reset_color();

    // let's now try to invalidate the block and read it again
    print_color_bold(YELLOW);
    printf("\nTrying to invalidate the block and read it again ...\n");
    reset_color();

    ret = invalidate_data(num_blocks - 1);
    if(ret < 0){
        // this error shouldn't happen
        print_color_bold(RED);
        printf("\ninvalidate_data() unexpectedly failed\n");
        reset_color();
        exit(1);
    }

    ret = get_data(num_blocks - 1, buffer, MAX_MSG_SIZE);
    if(!((ret < 0) && (errno == ENODATA))){
        // this error shouldn't happen
        print_color_bold(RED);
        printf("\nENODATA was expected, but get_data() succeded or returned a differnt kind of error\n");
        reset_color();
        exit(1);
    }

    print_color(GREEN);
    printf("get_data() returned ENODATA, as expected.\n");
    reset_color();

    return 0;
}