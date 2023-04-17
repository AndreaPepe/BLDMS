CC := gcc
CFLAGS := -Wall -Werror

KERNELDIR := /lib/modules/$(shell uname -r)/build
EXTRA_CFLAGS := -I$(KERNELDIR)/include
export C_INCLUDE_PATH := /usr/src/linux-headers-$(uname -r)/include

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
