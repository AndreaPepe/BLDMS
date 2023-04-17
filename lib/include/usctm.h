#pragma once
#ifndef __USCTM_H__
#define __USCTM_H__

void unprotect_memory(void);
void protect_memory(void);
int get_entries(int *, int*, int, unsigned long *, unsigned long *);
void reset_entries(int *, int *, int);

#endif