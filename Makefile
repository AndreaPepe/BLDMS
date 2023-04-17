obj-m += the_bldms.o
the_bldms-objs += bldms.o file_ops.o dir_ops.o rcu.o syscalls.o lib/usctm.o

DEVICE_TYPE := "bldms_fs"
BLOCK_SIZE := 4096
NBLOCKS := 1000
NR_BLOCKS_FORMAT := 102

SYSCALL_TABLE = $(shell cat /sys/module/the_usctm/parameters/sys_call_table_address)
NUM_SYSCALL_TABLE_ENTRIES = $(shell cat /sys/module/the_usctm/parameters/num_entries_found)
SYS_NI_SYSCALL = $(shell cat /sys/module/the_usctm/parameters/sys_ni_syscall_address)
FREE_ENTRIES = $(shell cat /sys/module/the_usctm/parameters/free_entries)


all:
	gcc bldmsmakefs.c -lrt -o bldmsmakefs
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	rm bldmsmakefs
	rmdir mount

create-fs:
	dd bs=$(BLOCK_SIZE) count=$(NR_BLOCKS_FORMAT) if=/dev/zero of=image
	./bldmsmakefs image
	mkdir mount

mount-fs:
	mount -o loop -t $(DEVICE_TYPE) image ./mount/

umount-fs:
	umount ./mount

insmod:
	insmod the_bldms.ko sys_call_table_address=$(SYSCALL_TABLE) sys_ni_syscall_address=$(SYSCALL_TABLE) free_entries=$(FREE_ENTRIES) num_entries_found=$(NUM_SYSCALL_TABLE_ENTRIES)

rmmod:
	rmmod the_bldms
	dmesg -C