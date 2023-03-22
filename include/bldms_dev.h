#pragma once
#ifndef __BLDMS_DEV_H__
#define __BLDMS_DEV_H__

#include <linux/fs.h>
#include <linux/genhd.h>

static struct blmds_dev{
    struct gendisk *gd;
} the_dev;

#endif