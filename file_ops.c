#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/timekeeping.h>
#include <linux/time.h>
#include <linux/buffer_head.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "include/bldms.h"

//look up goes in the inode operations
const struct inode_operations bldms_inode_ops = {
        //.lookup = onefilefs_lookup,
};

const struct file_operations bldms_file_operations = {
        .owner = THIS_MODULE,
        //.read = onefilefs_read,
        //.write = onefilefs_write //please implement this function to complete the exercise
};
