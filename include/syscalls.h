#pragma once
#ifndef __BLDMS_SYSCALLS_H__
#define __BLDMS_SYSCALLS_H__

#define SYNCHRONOUS_PUT_DATA 1

int register_syscalls(void);
void unregister_syscalls(void);

#endif