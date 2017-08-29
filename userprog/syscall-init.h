#ifndef __USERPROG_SYSCALL_INIT_H
#define __USERPROG_SYSCALL_INIT_H

#include "stdint.h"

void syscall_init();
uint32_t sys_getpid();
// uint32_t sys_write(char *str);
#endif