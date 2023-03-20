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

//add the iterate function in the dir operations
const struct file_operations bldms_dir_operations = {
        .owner = THIS_MODULE,
        //.iterate = onefilefs_iterate,
};
