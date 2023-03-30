#pragma once
#ifndef __BLDMS_DEV_H__
#define __BLDMS_DEV_H__

#include <linux/version.h>
#include <include/bldms.h>

#define EXPORT_SYMTAB
#define DEVICE_NAME "bldms-dev"
#define MINORS 8
#define OBJECT_MAX_SIZE (4096)

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 0, 0)
#define get_major(session)	MAJOR(session->f_inode->i_rdev)
#define get_minor(session)	MINOR(session->f_inode->i_rdev)
#else
#define get_major(session)	MAJOR(session->f_dentry->d_inode->i_rdev)
#define get_minor(session)	MINOR(session->f_dentry->d_inode->i_rdev)
#endif

#endif