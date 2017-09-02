#include "syscall-init.h"
#include "thread.h"
#include "syscall.h"
#include "print.h"
#include "console.h"
#include "string.h"
#include "memory.h"
#include "fs.h"
#include "fork.h"

#define syscall_nr 32

typedef void * syscall;

extern void cls_screen();

syscall syscall_table[syscall_nr];

uint32_t sys_getpid() {
    return running_thread()->pid;
}

void sys_putchar(char char_ascii) {
    console_put_char(char_ascii);
}

// 初始化系统调用
void syscall_init() {
    put_str("syscall_init_start\n");
    syscall_table[SYS_GETPID] = sys_getpid;
    syscall_table[SYS_WRITE] = sys_write;
    syscall_table[SYS_MALLOC] = sys_malloc;
    syscall_table[SYS_FREE] = sys_free;
    syscall_table[SYS_FORK] = sys_fork;
    syscall_table[SYS_READ] = sys_read;
    syscall_table[SYS_CLEAR] = cls_screen;
    syscall_table[SYS_PUTCHAR] = sys_putchar;
    put_str("syscall_init done\n");
}