obj-m += the_bldms.o
the_bldms-objs += bldms.o bldms_fs.o file_ops.o dir_ops.o

DEVICE_TYPE := "bldms"
BLOCK_SIZE := 4096
NBLOCKS := 100

all:
	#gcc bldms.c -o bldms
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	# rm onefilemakefs

create-fs:
	dd bs=$(BLOCK_SIZE) count=$(NBLOCKS) if=/dev/zero of=image
	#./singlefilemakefs image
	#mkdir mount

mount-fs:
	mount -o loop -t $(DEVICE_TYPE) image ./mount/