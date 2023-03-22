#include <linux/module.h>
#include <linux/printk.h>
#include <linux/fs.h>
#include <linux/blkdev.h>

#include "include/bldms.h"

static int __init bldms_init(void){
    int status;

    status = register_blkdev(BLDMS_MAJOR, BDEV_NAME);
    if(status < 0){
        printk(KERN_ERR "Unable to register %s device\n", BDEV_NAME);
        return -EBUSY;
    }

    return 0;
}

static void __exit bldms_exit(void){
    unregister_blkdev(BLDMS_MAJOR, BDEV_NAME);
}

