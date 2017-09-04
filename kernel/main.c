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
    // 写入用户进程
    uint32_t file_size = 5307; 
    uint32_t sec_cnt = DIV_ROUND_UP(file_size, 512);
    struct disk* sda = &channels[0].devices[0];
    void* prog_buf = sys_malloc(file_size);
    ide_read(sda, 300, prog_buf, sec_cnt);
    int32_t fd = sys_open("/prog_arg", O_CREATE | O_RDWR);
    if (fd != -1) {
        if(sys_write(fd, prog_buf, file_size) == -1) {
            printk("file write error!\n");
            while(1);
        }
    }

    file_size = 4777;
    sec_cnt = DIV_ROUND_UP(file_size, 512);
    sda = &channels[0].devices[0];
    prog_buf = sys_malloc(file_size);
    ide_read(sda, 600, prog_buf, sec_cnt);
    fd = sys_open("/prog_no_arg", O_CREATE | O_RDWR);
    if (fd != -1) {
        if(sys_write(fd, prog_buf, file_size) == -1) {
            printk("file write error!\n");
            while(1);
        }
    }
    
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
