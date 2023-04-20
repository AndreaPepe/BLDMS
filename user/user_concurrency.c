#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/stat.h>
#include "include/pretty-print.h"
#include "include/quotes.h"

#define READERS 2
#define GETTERS 4
#define WRITERS 2
#define INVALIDATORS 2
#define NUM_SPAWNS (READERS + GETTERS + WRITERS + INVALIDATORS)

#define METADATA_SIZE (sizeof(signed long long) + sizeof(int) + sizeof(unsigned char))
#define BLK_SIZE (1 << 12)
#define MAX_MSG_SIZE (BLK_SIZE - METADATA_SIZE)

long put_data_nr = 0x0;
long get_data_nr = 0x0;
long invalidate_data_nr = 0x0;
char *device_filepath;
size_t num_blocks = 0x0;


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
                print_color(YELLOW);
                printf("[Getter %lu]:\tget_data() on block %d returned ENODATA\n", param, to_get);
                reset_color();
            }else{
                print_color_bold(RED);
                printf("[Getter %lu]:\tget_data() on block %d has returned with error\n", param, to_get);
                reset_color();
            }   
        }else{
            print_color(CYAN);
                printf("[Getter %lu]:\tget_data() on block %d read the following %d bytes:%s\n", param, to_get, ret, buffer);
            reset_color();
        }
    }
}

/*
* Operations of thread exploiting the put_data() system call
*/
void *writer(void *arg){

}

/*
* Operations of thread exploiting the invalidate_data() system call
*/
void *invalidator(void *arg){

}

/*
* Operations of thread reading from the device as a file
*/
void *reader(void *arg){

}

int main(int argc, char **argv){
    pthread_t tids[NUM_SPAWNS];
    int block_ids[NUM_MESSAGES] = {-1,}; 
    int i, ret;
    struct stat *st;
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
    stat(device_filepath, st);
    //FIXME!!! Compute or take as argument the number of blocks
    num_blocks = 100;
    printf("Device have %ld blocks\n", num_blocks);

    // TODO: spawn threads to do the work concurrently and wait them to finish
    printf("Spawning workers...\n\n");
    for(i=0; i < NUM_SPAWNS; i++){
        //TODO: change this statement in order to spawn only the right number of getters
        pthread_create(&tids[i], NULL, getter, NULL);
    }

    for(i=0; i < NUM_SPAWNS; i++){
        //TODO: change this statement in order to spawn only the right number of getters
        pthread_join(tids[i], NULL);
    }

    return 0;
}