#include "syscall-init.h"
#include "thread.h"
#include "syscall.h"
#include "print.h"


#define syscall_nr 32

typedef void * syscall;

syscall syscall_table[syscall_nr];

uint32_t sys_getpid() {
    return running_thread()->pid;
}

// 初始化系统调用
void syscall_init() {
    put_str("syscall_init_start\n");
    syscall_table[SYS_GETPID] = sys_getpid;
    put_str("syscall_init done\n");
}