#pragma once
#ifndef __BLDMS_SYSCALLS_H__
#define __BLDMS_SYSCALLS_H__

// compilation-time directive that allows to choose if write operations on the device will be synchronous or not
#ifndef SYNCHRONOUS_PUT_DATA
    #define SYNCHRONOUS_PUT_DATA 1
#endif

int register_syscalls(void);
void unregister_syscalls(void);

#endif