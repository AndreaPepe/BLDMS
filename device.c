/**
 * Baseline char device driver
 * 
 * WARNING: for now this is a SINGLE_SESSION_OBJECT: just 1 session per I/O node at a time
*/

#include <linux/version.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>

#include "include/device.h"

// device function prototypes
static int dev_open(struct inode *, struct file *);
static int dev_release(struct inode *, struct file *);
static ssize_t dev_write(struct file *, const char *, size_t, loff_t *);
static ssize_t dev_read(struct file *, const char *, size_t, loff_t *);
static long dev_ioctl(struct file *, unsigned int, unsigned long);

static int Major;

DEFINE_MUTEX(dev_state);

typedef struct _object_state{
    struct mutex object_busy;
    struct mutex operation_synchronizer;
    int valid_bytes;
    char *content;                          // the I/O node is a buffer in memory
}object_state;

object_state objects[MINORS];


/* the actual driver implementation */

static int dev_open(struct inode *inode, struct file *file){
    int minor = get_minor(file);
    if(minor >= MINORS){
        // cap reached: no free object available
        return -ENODEV;
    }

    // single instance device
    if(!mutex_trylock(&dev_state)){
        return -EBUSY;
    }

    if(!mutex_trylock(&(objects[minor].object_busy))){
        mutex_unlock(&dev_state);
        return -EBUSY;
    }

    printk("%s: %s device open successfully\n", MOD_NAME, DEVICE_NAME);
    return 0;
}


static int dev_release(struct inode *inode, struct file *file){
    int minor = get_minor(file);

    // unlock the busy mutex of the object associated with the given minor number
    mutex_unlock(&(objects[minor].object_busy));

    // unlock the single-instance device's busy mutex
    mutex_unlock(&dev_state);

    printk("%s: %s device closed\n", MOD_NAME, DEVICE_NAME);
    return 0;
}


static ssize_t dev_write(struct file *filp, const char *buff, size_t len, loff_t *off){
    int minor = get_minor(filp);
    ssize_t ret;
    object_state *the_object;

    // take the right object
    the_object = objects + minor;
    printk("%s: a write on device with [%d-%d] major-minor numbers has been called\n", MOD_NAME, get_major(filp), minor);

    // synchronize the write by taking a lock on the operation synchronizer mutex
    mutex_lock(&(the_object->operation_synchronizer));

    // check if the offset is too large
    if (*off >= OBJECT_MAX_SIZE){
        mutex_unlock(&(the_object->operation_synchronizer));
        return -ENOSPC;
    }

    // check if the offset surpassed the valid bytes
    if (*off > the_object->valid_bytes){
        mutex_unlock(&(the_object->operation_synchronizer));
        return -ENOSR;
    }

    // if the length goes beyond the end of the device, trunk it
    if (*off + len > OBJECT_MAX_SIZE)
        len = OBJECT_MAX_SIZE - *off;

    // get the data to be written from user space
    ret = copy_from_user(&(the_object->content[*off]), buff, len);

    // increment the offset
    *off += (len - ret);

    // increment the valid bytes
    the_object->valid_bytes = *off;
    mutex_unlock(&(the_object->operation_synchronizer));

    // return the number of bytes that still need to be written
    return (len - ret);
}


static ssize_t dev_read(struct file *filp, __user const char *buff, size_t len, loff_t *off){
    int minor = get_minor(filp);
    ssize_t ret;
    object_state *the_object;

    the_object = objects + minor;
    mutex_lock(&(the_object->operation_synchronizer));

    // check if offset goes beyond the valid bytes
    if (*off > the_object->valid_bytes){
        mutex_unlock(&(the_object->operation_synchronizer));
        // return 0, because there is no more data to read
        return 0;
    }

    // check if the length goes beyond the valid bytes
    if (*off + len > the_object->valid_bytes)
        len = the_object->valid_bytes - *off;

    // copy the content of the object into the user space specified buffer
    ret = copy_to_user(buff, the_object->content[*off], len);

    mutex_unlock(&(the_object->operation_synchronizer));
    // return the number of bytes that still need to be read
    return (len - ret);
}

static long dev_ioctl(struct file *filp, unsigned int command, unsigned long param){
    int minor = get_minor(filp);
    object_state *the_object;

    the_object = objects + minor;
    printk("%s: an ioctl has been called on device %s with command %u\n", MOD_NAME, DEVICE_NAME, command);

    // TODO: add something to do here, maybe check on the device size upon mounting it

    return 0;
}


static struct file_operations fops = {
  .owner = THIS_MODULE,
  .write = dev_write,
  .read = dev_read,
  .open =  dev_open,
  .release = dev_release,
  .unlocked_ioctl = dev_ioctl
};


//TODO: consider a separate module for the device only

// init the device
int dev_init(){

  return 0;  
}

// cleanup the device
void dev_cleanup(){
    return;
}