#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#define READERS 2
#define GETTERS 4
#define WRITERS 2
#define INVALIDATORS 2
#define NUM_SPAWNS (READERS + GETTERS + WRITERS + INVALIDATORS)
#define MAX_MSG_SIZE (1 << 12)

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

#define NUM_MESSAGES 4

char *messages[NUM_MESSAGES] = {
    "Veni, vidi, vici. ~ G. Cesare\n",
    "The sun is the same in a relative way, but we are older, shorter of breath, one day closer to death\n",
    "I'm holding out for a hero 'til the end of the light;\n\the's gonna be strong, he's gonna be fast, he's gonna be fresh from the fire.\n\t~B. Tyler (I'm Holding Out For A Hero)\n",
    "Nun t preoccupà waglio, c' sta o' mar for, c' sta o mar for'...\n"
};


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

    // TODO: spawn threads to do the work concurrently and wait them to finish
    printf("Putting messages ...\n");
    for(i=0; i < NUM_MESSAGES; i++){
        ret = put_data(messages[i], strlen(messages[i]) + 1);
        printf("[Message %d] %s", i, messages[i]);
        if (ret < 0){
            printf("put_data() returned error - (%d) %s\n", errno, strerror(errno));
            return ret;
        }
        block_ids[i] = ret;
    }

    printf("\n\nInvalidating some messages ...\n");
    for(i = 0; i < NUM_MESSAGES; i = i+2){
        ret = invalidate_data(block_ids[i]);
        if (ret < 0){
            printf("invalidate_data() returned error - (%d) %s\n", errno, strerror(errno));
            return ret;
        }
        printf("Message in block %d correctly invalidated\n", block_ids[i]);
    }

    printf("\nGetting data ...\n");
    ret = get_data(0, &buffer, MAX_MSG_SIZE);
    if (ret < 0){
        printf("get_data() on block of index 0 returned error - (%d) %s\n", errno, strerror(errno));
        return ret;
    }
    printf("get_data() on block index 0 read %d bytes and the following content:\n%s", ret, buffer);

    ret = get_data(76, &buffer, MAX_MSG_SIZE);
    if (ret < 0){
        if(errno == ENODATA){
            printf("get_data() on block of index 76 returned ENODATA, as expected!\n");
        }else{
            printf("get_data() on block of index 76 returned error - (%d) %s\n", errno, strerror(errno));
            return ret;
        }
        
    }else{
        printf("get_data() on block index 76 read %d bytes and the following content (but it was NOT EXPECTED!):\n%s", ret, buffer);
        return 1;
    }

    printf("All operations completed correctly\n");
    return 0;
}