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
 * @file user_concurrency.c
 * @brief Program for the interaction form user space with the BLDMS service.
 * Several threads are spawn to perform different operation concurrently on the device:
 *      - readers will access the device as a file and read its content;
 *      - getters will access in read mode the device trying to read the content of specific blocks,
 *        making use of the get_data() system call;
 *      - writers will try to add new messages to the device, through the put_data() system call;
 *      - invalidators will try to invalidate some specific block of the device, making them no more
 *        available for read operations, but, instead, available for future write operations, through
 *        the invokation of the invalidate_data() system call.
 * 
 * The program will check if some error in the driver functions happens. The output of the program is a
 * series of logging messages of the different threads.
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
#include <pthread.h>
#include <sys/stat.h>
#include <stdint.h>
#include "include/pretty-print.h"
#include "include/quotes.h"

// number of thread spwaned for each category: change these as you like
#define READERS 1
#define GETTERS 2
#define WRITERS 1
#define INVALIDATORS 1
#define NUM_SPAWNS (READERS + GETTERS + WRITERS + INVALIDATORS)

#define METADATA_SIZE (sizeof(signed long long) + sizeof(uint16_t))
#define BLK_SIZE (1 << 12)
#define MAX_MSG_SIZE (BLK_SIZE - METADATA_SIZE)

long put_data_nr = 0x0;
long get_data_nr = 0x0;
long invalidate_data_nr = 0x0;
char *device_filepath;
size_t num_blocks = 0x0;

int total_errors = 0;                           // global variable to check for errors

// declaration of macros for calling the system calls
#define put_data(source, size) \
            syscall(put_data_nr, source, size)

#define get_data(offset, destination, size) \
            syscall(get_data_nr, offset, destination, size)

#define invalidate_data(offset) \
            syscall(invalidate_data_nr, offset)


/*
* Operations of thread exploiting the get_data() system call
*/
void *getter(void *arg){
    int ret, i, to_get;
    unsigned long param = pthread_self();
    char buffer[MAX_MSG_SIZE] = {0x0,};

    
    for(i=0; i < num_blocks; i++){
        if(param % 2){
            // if odd parameter, try to get all blocks in block's index order
            to_get = i;
        }else{
            // if even parameter, try to get all blocks in reverse block's index order
            to_get = (num_blocks - 1) - i;
        }
        ret = get_data(to_get, buffer, MAX_MSG_SIZE);
        if(ret < 0){
            if(errno == ENODATA){
                printf("%s[Getter %lu]:%s\tget_data() on block %d returned ENODATA\n", YELLOW_STR, param, DEFAULT_STR, to_get);
            }else{
                printf("%s[Getter %lu]:\tget_data() on block %d has returned with error%s\n", RED_STR, param, to_get, DEFAULT_STR);
                total_errors++;
            }   
        }else{
                printf("%s[Getter %lu]:%s\tget_data() on block %d read the following %d bytes:%s\n", YELLOW_STR, param, DEFAULT_STR, to_get, ret, buffer);
        }
        fflush(stdout);
    }
    return NULL;
}

/*
* Operations of thread exploiting the put_data() system call
*/
void *writer(void *arg){
    int ret, i, num_iterations;
    // use the own TID as parameter for the task
    unsigned long param = pthread_self();
    char *msg;

    num_iterations = num_blocks / 5;
    
    for (i=0; i < num_iterations; i++){
        msg = messages[(param + i) % NUM_MESSAGES];
        ret = put_data(msg, strlen(msg) + 1);

        if(ret < 0){
            if(errno == ENOMEM){
                printf("%s[Writer %lu]:%s\tput_data() returned ENOMEM\n", MAGENTA_STR, param, DEFAULT_STR);
            }else{
                printf("%s[Writer %lu]:\tput_data() returned an error%s\n", RED_STR, param, DEFAULT_STR);
                total_errors++;
            }   
        }else{
                printf("%s[Writer %lu]:%s\tput_data() successful - the message has been written in block %d\n", MAGENTA_STR, param, DEFAULT_STR,ret);
        }
        fflush(stdout);
    }
    return NULL;
}

/*
* Operations of thread exploiting the invalidate_data() system call
*/
void *invalidator(void *arg){
    int ret, i, num_invalidations, to_invalidate;
    unsigned long param = pthread_self();

    num_invalidations = num_blocks / 8;

    for (i=0; i < num_invalidations; i++){
        to_invalidate = (param + i) % num_blocks;

        ret = invalidate_data(to_invalidate);
        if (ret < 0){
            if (errno == ENODATA){
                printf("%s[Invalidator %lu]:%s\tinvalidate_data() on block %d returned ENODATA\n", BLUE_STR, param, DEFAULT_STR, to_invalidate);
            }else{
                printf("%s[Writer %lu]:\tinvalidate_data() on block %d returned an error%s\n", RED_STR, param, to_invalidate, DEFAULT_STR);
                total_errors++;
            }
        }else{
            printf("%s[Invalidator %lu]:%s\tinvalidate_data() on block %d executed correctly\n", BLUE_STR, param, DEFAULT_STR, to_invalidate);
        }
        fflush(stdout);
    }

    return NULL;
}

/*
* Operations of thread reading from the device as a file
*/
void *reader(void *arg){
    int i, ret, fd, num_loops;
    unsigned long param = pthread_self();
    char buffer[MAX_MSG_SIZE] = {0x0,};
    
    fd = open(device_filepath, O_RDONLY);
    if (fd < 0){
        printf("%s[Reader %lu]:\tunable to open device as a file%s\n", RED_STR, param, DEFAULT_STR);
        total_errors++;
        fflush(stdout);
        return NULL;
    }

    num_loops = 3;
    for(i = 0; i < num_loops; i++){
        
        printf("%s[Reader %lu]:\tstart reading%s\n", CYAN_STR, param, DEFAULT_STR);
        // read all the messages from the device num_loops times
        while((ret = read(fd, buffer, MAX_MSG_SIZE)) != 0){
            if(ret < 0){
                printf("%s[Reader %lu]:\tread() returned with error%s\n", RED_STR, param, DEFAULT_STR);
                total_errors++;
            }else{
                printf("%s[Reader %lu]:\tread() has read the following %d bytes:%s\n%s\n", CYAN_STR, param, ret, DEFAULT_STR, buffer);
            }
            fflush(stdout);
            memset(buffer, 0, MAX_MSG_SIZE);
        }
        // reset the session to begin reading again all messages
        lseek(fd, 0, SEEK_SET);
        sleep(1);       
    }
    close(fd);
    
    return NULL;   
}

int main(int argc, char **argv){
    pthread_t tids[NUM_SPAWNS];
    int block_ids[NUM_MESSAGES] = {-1,}; 
    int i, ret;
    struct stat st;
    char buffer[MAX_MSG_SIZE] = {0,};

    if(argc < 5){
        printf("Usage:\n\t./%s <device file path> <put_data() NR> <get_data() NR> <invalidate_data() NR\n\n", argv[0]);
        exit(1);
    }
    printf("Initializing test program ...\n\n");
    // save device file location and system call numbers
    device_filepath = argv[1];
    put_data_nr = atol(argv[2]);
    get_data_nr = atol(argv[3]);
    invalidate_data_nr = atol(argv[4]);

    int fd = open(device_filepath, O_RDONLY);
    if(fd < 0){
        print_color(RED);
        printf("Error: unable to open device as a file\n");
        reset_color();
        exit(1);
    }

    // compute the number of blocks of the device, given that we know the block size
    fstat(fd, &st);
    num_blocks = st.st_size / BLK_SIZE;
    printf("Device have %ld blocks\n", num_blocks);
    close(fd);

    // spawn threads to do the work concurrently and wait them to finish
    printf("Spawning workers...\n\n");
    for(i=0; i < NUM_SPAWNS; i++){
        if (i < GETTERS)
            pthread_create(&tids[i], NULL, getter, NULL);
        else if (i < GETTERS + WRITERS)
            pthread_create(&tids[i], NULL, writer, NULL);
        else if (i < GETTERS + WRITERS + INVALIDATORS)
            pthread_create(&tids[i], NULL, invalidator, NULL);
        else
            pthread_create(&tids[i], NULL, reader, NULL);
    }

    // wait for threads to finish their job
    for(i=0; i < NUM_SPAWNS; i++){
        pthread_join(tids[i], NULL);
    }

    if(total_errors == 0){
        print_color(GREEN);
        printf("\nProgram executed correctly!\n");
        reset_color();
        return 0;
    }else{
        print_color_bold(RED);
        printf("\nProgram executed but encountered %d errors :(\n", total_errors);
        reset_color();
        return total_errors;
    }
    
}