#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

#define READERS 2
#define GETTERS 4
#define WRITERS 2
#define INVALIDATORS 2
#define NUM_SPAWNS (READERS + GETTERS + WRITERS + INVALIDATORS)

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
    int tids[NUM_SPAWNS] = {-1,};
    int i;

    if(argc < 5){
        printf("Usage:\n\t./%s <device file path> <put_data() NR> <get_data() NR> <invalidate_data() NR\n\n", argv[0]);
        exit(1);
    }

    // save device file location and system call numbers
    device_filepath = argv[1];
    put_data_nr = atol(argv[2]);
    get_data_nr = atol(argv[3]);
    invalidate_data_nr = atol(argv[4]);

    // TODO: spawn threads to do the work concurrently and wait them to finish



    return 0;
}