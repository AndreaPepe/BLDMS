KERNELDIR := /lib/modules/$(shell uname -r)/build

obj-m += the_usctm.o
the_usctm-objs += usctm.o ./lib/vtpmo.o

all:
	make -C $(KERNELDIR) M=$(PWD) modules

clean:
	make -C $(KERNELDIR) M=$(PWD) clean

insmod:
	insmod the_usctm.ko

rmmod:
	rmmod the_usctm
