#include "init.h"
#include "print.h"
#include "interrupt.h"
#include "timer.h"
#include "memory.h"
#include "thread.h"
#include "console.h"
#include "keyboard.h"
#include "tss.h"
#include "syscall-init.h"
#include "ide.h"
#include "fs.h"

void init_all() {
    put_str("init_all\n");
    idt_init(); // 初始化中断
    timer_init(); // 初始化定时器8253
    mem_init(); // 初始化内存池管理
    thread_init(); // 初始化多线程环境
    console_init(); // 初始化控制台
    keyboard_init(); // 初始化键盘
    tss_init(); // 初始化TSS，重载GDT
    syscall_init(); // 初始化系统调用
    ide_init(); // 初始化硬盘驱动
    filesys_init(); // 初始化文件系统
}