#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/syscalls.h>

#include "lib/include/usctm.h"  
#include "include/bldms.h"

unsigned long the_syscall_table = 0x0;

unsigned long the_ni_syscall;

unsigned long new_sys_call_array[] = {0x0, 0x0, 0x0};
#define HACKED_ENTRIES (int)(sizeof(new_sys_call_array)/sizeof(unsigned long))
int restore_entries[HACKED_ENTRIES] = {[0 ... (HACKED_ENTRIES-1)] -1};
int indexes[HACKED_ENTRIES] = {[0 ... (HACKED_ENTRIES-1)] -1};

// put_data() system call
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 17, 0)
__SYSCALL_DEFINEx(2, _put_data, char *, source, size_t, size){
#else
asmlinkage int sys_put_data(char *source, size_t size){
#endif
    return 0;
}

// get_data() system call
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 17, 0)
__SYSCALL_DEFINEx(3, _get_data, int, offset, char *, destination, size_t, size){
#else
asmlinkage int sys_get_data(int offset, char *destination, size_t size){
#endif
    return 0;
}

// invalidate_data() system call
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 17, 0)
__SYSCALL_DEFINEx(1, _invalidate_data, int, offset){
#else
asmlinkage int sys_invalidate_data(int offset){
#endif
    return 0;
}


#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,17,0)
long sys_put_data = (unsigned long) __x64_sys_put_data;
long sys_get_data = (unsigned long) __x64_sys_get_data;
long sys_invalidate_data = (unsigned long) __x64_sys_invalidate_data;
#else
#endif


int register_syscalls(void){
    int ret, i;

    ret = get_entries(restore_entries, indexes, HACKED_ENTRIES, &the_syscall_table, &the_ni_syscall);
    if(ret != HACKED_ENTRIES){
        printk("%s: unable to register system calls - get_entries failed returning %d\n", MOD_NAME, ret);
        return -1;
    }

    /* the system calls will be installed in this order
     * 1. put_data();
     * 2. get_data();
     * 3. invalidate_data();
     */
    new_sys_call_array[0] = (unsigned long)sys_put_data;
    new_sys_call_array[1] = (unsigned long)sys_get_data;
    new_sys_call_array[2] = (unsigned long)sys_invalidate_data;

    unprotect_memory();
    for(i=0; i<HACKED_ENTRIES; i++){
        ((unsigned long *)the_syscall_table)[restore_entries[i]] = (unsigned long)new_sys_call_array[i];
    }
    protect_memory();

    printk("%s: all new system calls correctly installed on system-call table\n", MOD_NAME);
    return 0;
}


void unregister_syscalls(void){
    int i;

    unprotect_memory();
    for (i=0; i<HACKED_ENTRIES; i++){
        ((unsigned long *)the_syscall_table)[restore_entries[i]] = the_ni_syscall;
    }
    protect_memory();

    reset_entries(restore_entries, indexes, HACKED_ENTRIES);

    printk("%s: sys-call table restored to its original content\n", MOD_NAME);
}