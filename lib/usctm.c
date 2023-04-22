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
 * @file usctm.c 
 * @brief simple library for the interaction with the system call discoverer module
 * @author Andrea Pepe
 * @date April 22, 2023  
*/

#define EXPORT_SYMTAB
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/errno.h>
#include <linux/device.h>
#include <linux/kprobes.h>
#include <linux/mutex.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/interrupt.h>
#include <linux/time.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <asm/page.h>
#include <asm/cacheflush.h>
#include <asm/apic.h>
#include <linux/syscalls.h>

#define LIBNAME "USCTM"

unsigned long sys_call_table_address = 0x0;
module_param(sys_call_table_address, ulong, 0660);

unsigned long sys_ni_syscall_address = 0x0;
module_param(sys_ni_syscall_address, ulong, 0660);


#define MAX_FREE 15
#define MAX_ACQUIRES 4
int free_entries[MAX_FREE];
module_param_array(free_entries,int,NULL,0660);//default array size already known - here we expect to receive what entries are free
int num_entries_found;
module_param(num_entries_found, int, 0660);

int restore[MAX_ACQUIRES] = {[0 ... (MAX_ACQUIRES-1)] -1};

unsigned long cr0;

static inline void write_cr0_forced(unsigned long val){
    unsigned long __force_order;
    asm volatile(
        "mov %0, %%cr0"
        : "+r"(val), "+m"(__force_order));
}

inline void protect_memory(void){
    write_cr0_forced(cr0);
}

inline void unprotect_memory(void){
	cr0 = read_cr0();
    write_cr0_forced(cr0 & ~X86_CR0_WP);
}

/**
 * @brief  Get indexes of free usable system call table entries to install new system calls.
 */
int get_entries(int *entry_ids, int *entry_ndx, int num_acquires, unsigned long *syscall_table_addr, unsigned long *sys_ni_sys_call){
	int i, given;
	int ids[MAX_ACQUIRES] = {[0 ... (MAX_ACQUIRES-1)] -1};
	int indexes[MAX_ACQUIRES] = {[0 ... (MAX_ACQUIRES-1)] -1};

	if(num_acquires > num_entries_found || num_acquires > MAX_ACQUIRES){
		printk("%s: Not enough free entries available\n", LIBNAME);
		return -1;
	}

	if (num_acquires < 1){
		printk("%s: less than 1 sys-call table entry requested\n", LIBNAME);
		return -1;
	}

	given = 0;
	for(i=0; i<num_entries_found && given < num_acquires; i++){
		if (restore[i] == -1){
			// the entry is free
			restore[i] = free_entries[i];
			ids[i] = free_entries[i];
			indexes[i] = i;
			given++;
		}
	}

	if(given != num_acquires){
		printk("%s: something went wrong - given differs from number of requested sys-call table entries\n", LIBNAME);
		return -1;
	}

	*syscall_table_addr = sys_call_table_address;
	*sys_ni_sys_call = sys_ni_syscall_address;
	memcpy((char*)entry_ids, (char*)ids, given*sizeof(int));
	memcpy((char*)entry_ndx, (char*)indexes, given*sizeof(int));
	return given;
}

/**
 * @brief  Make system call table entries available again
 */
void reset_entries(int *entry_ids, int *entry_ndx, int num_resets){
	int i;

	for(i=0; i < num_resets; i++){
		restore[entry_ndx[i]] = -1;
		printk("%s: sys-call table entry %d became available again\n", LIBNAME, entry_ids[i]);
	}
	
	return;
}

