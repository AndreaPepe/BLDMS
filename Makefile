obj-m += the_bldms.o
the_bldms-objs += bldms.o file_ops.o dir_ops.o rcu.o

DEVICE_TYPE := "bldms_fs"
BLOCK_SIZE := 4096
NBLOCKS := 1000
NR_BLOCKS_FORMAT := 100

all:
	gcc bldmsmakefs.c -o bldmsmakefs
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