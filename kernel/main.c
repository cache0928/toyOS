#include "print.h"
#include "init.h"
#include "thread.h"
#include "interrupt.h"
#include "console.h"
#include "process.h"
#include "syscall-init.h"
#include "syscall.h"
#include "stdio-kernel.h"
#include "stdio.h"
#include "memory.h"
#include "fs.h"
#include "string.h"
#include "dir.h"
#include "shell.h"
extern void cls_screen();
int main() {
    put_str("I am kernel\n");
    init_all();
    cls_screen();
    console_put_str("<toyOS:/ cache> $ ");
    intr_enable();
    while(1);
    return 0;
}
// init进程
void init() {
    // void *addr = malloc(256);
    uint32_t ret_pid = fork();
    if(ret_pid) {
        // 父进程
        while(1);
    } else {
        // 子进程
        my_shell();
    }
    while(1);
}
