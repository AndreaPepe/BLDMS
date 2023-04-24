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
 * @file user.c
 * @brief Basic program for the interaction form user space with the BLDMS service.
 * 
 * @author Andrea Pepe
 * @date April 22, 2023  
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include "include/pretty-print.h"
#include "include/quotes.h"

#define READERS 2
#define GETTERS 4
#define WRITERS 2
#define INVALIDATORS 2
#define NUM_SPAWNS (READERS + GETTERS + WRITERS + INVALIDATORS)
#define METADATA_SIZE (sizeof(signed long long) + sizeof(int) + sizeof(unsigned char))
#define MAX_MSG_SIZE ((1 << 12) - METADATA_SIZE)

long put_data_nr = 0x0;
long get_data_nr = 0x0;
long invalidate_data_nr = 0x0;
char *device_filepath;

// declaration of macros for calling the system calls
#define put_data(source, size) \
            syscall(put_data_nr, source, size)

#define get_data(offset, destination, size) \
            syscall(get_data_nr, offset, destination, size)

#define invalidate_data(offset) \
            syscall(invalidate_data_nr, offset)




int main(int argc, char **argv){
    int block_ids[NUM_MESSAGES] = {-1,}; 
    int i, ret;
    char buffer[MAX_MSG_SIZE] = {0,};

    if(argc < 5){
        printf("Usage:\n\t./%s <device file path> <put_data() NR> <get_data() NR> <invalidate_data() NR\n\n", argv[0]);
        exit(1);
    }

    // save device file location and system call numbers
    device_filepath = argv[1];
    put_data_nr = atol(argv[2]);
    get_data_nr = atol(argv[3]);
    invalidate_data_nr = atol(argv[4]);

    
    print_color_bold(YELLOW);
    printf("Putting messages ...\n");
    reset_color();
    for(i=0; i < NUM_MESSAGES; i++){
        ret = put_data(messages[i], strlen(messages[i]) + 1);
        if (ret < 0){
            printf("put_data() returned error - (%d) %s\n", errno, strerror(errno));
            return ret;
        }
        print_color(BLUE);
        printf("[Message %d added in block %d] ", i, ret);
        reset_color();
        printf("%s", messages[i]);
        // save the block index where the message has been added
        block_ids[i] = ret;
    }

    print_color_bold(YELLOW);
    printf("\n\nInvalidating some messages ...\n");
    reset_color();
    // invalidate blocks previously added as 1st, 3rd, 5th, ... and so on
    for(i = 0; i < NUM_MESSAGES; i = i+2){
        ret = invalidate_data(block_ids[i]);
        if (ret < 0){
            printf("invalidate_data() on block %d returned error - (%d) %s\n", block_ids[i], errno, strerror(errno));
            return ret;
        }
        printf("Message in block %d correctly invalidated\n", block_ids[i]);
    }

    print_color_bold(YELLOW);
    printf("\nGetting data ...\n");
    reset_color();
    // get the message in the block in which the 4th put_data() wrote
    ret = get_data(block_ids[3], &buffer, MAX_MSG_SIZE);
    if (ret < 0){
        printf("get_data() on block of index %d returned error - (%d) %s\n", block_ids[3], errno, strerror(errno));
        return ret;
    }
    printf("get_data() on block index %d read %d bytes and the following content:\n%s", block_ids[3], ret, buffer);

    // try to get the first previously added block: it should have been invalidated, so the get_data should set errno to ENODATA
    ret = get_data(block_ids[0], &buffer, MAX_MSG_SIZE);
    if (ret < 0){
        if(errno == ENODATA){
            printf("get_data() on block of index %d returned ENODATA, as expected!\n", block_ids[0]);
        }else{
            printf("get_data() on block of index %d returned error - (%d) %s\n", block_ids[0], errno, strerror(errno));
            return ret;
        }
        
    }else{
        printf("get_data() on block index %d read %d bytes and the following content (but it was NOT EXPECTED!):\n%s", block_ids[0], ret, buffer);
        exit(1);
    }

    print_color_bold(YELLOW);
    printf("\nReading data from the device as a file ...\n");
    reset_color();

    int fd = open(device_filepath, O_RDONLY);
    if(fd < 0){
        print_color(RED);
        printf("Error: unable to open device as a file\n");
        reset_color();
        exit(1);
    }


    // read all the currently valid messages on the device, accessing it as a file
    memset(buffer, 0, MAX_MSG_SIZE);
    while((ret = read(fd, buffer, MAX_MSG_SIZE)) != 0){
        if(ret < 0){
            print_color(RED);
            printf("Error: read() returned with error\n");
            reset_color();
            exit(1);
        }
        print_color(MAGENTA);
        printf("\nread() has read the following %d bytes:\n", ret);
        reset_color();
        printf("%s", buffer);
        memset(buffer, 0, MAX_MSG_SIZE);
    }


    close(fd);

    print_color(GREEN);
    printf("\nAll operations completed correctly\n\n");
    reset_color();
    return 0;
}