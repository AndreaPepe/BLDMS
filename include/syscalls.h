#pragma once
#ifndef __BLDMS_SYSCALLS_H__
#define __BLDMS_SYSCALLS_H__

#ifndef SYNCHRONOUS_PUT_DATA
    #define SYNCHRONOUS_PUT_DATA 1
#endif

int register_syscalls(void);
void unregister_syscalls(void);

#endif