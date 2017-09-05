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
#include "file.h"

extern void cls_screen();
int main() {
    put_str("I am kernel\n");
    init_all();
// 写入用户进程cat
    // uint32_t file_size = 5895; 
    // uint32_t sec_cnt = DIV_ROUND_UP(file_size, 512);
    // struct disk *sda = &channels[0].devices[0];
    // void* prog_buf = sys_malloc(file_size);
    // ide_read(sda, 300, prog_buf, sec_cnt);
    // int32_t fd = sys_open("/cat", O_CREATE | O_RDWR);
    // if (fd != -1) {
    //     if(sys_write(fd, prog_buf, file_size) == -1) {
    //         printk("file write error!\n");
    //         while(1);
    //     }
    // }
    cls_screen();
    console_put_str("<toyOS:/ cache> $ ");
    intr_enable();
    thread_exit(running_thread(), true);
    return 0;
}
// init进程
void init() {
    // void *addr = malloc(256);
    uint32_t ret_pid = fork();
    if(ret_pid) {
        // 父进程
        int status;
        int child_pid;
        while(1) {
            // 不断循环回收过继给他的孤儿进程
            child_pid = wait(&status);
            printf("I`m init, My pid is 1, I recieve a child, It`s pid is %d, status is %d\n", child_pid, status);
        }
    } else {
        // 子进程
        my_shell();
    }
    while(1);
}
