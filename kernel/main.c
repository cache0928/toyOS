#include "print.h"
#include "init.h"
#include "thread.h"
#include "interrupt.h"
#include "console.h"
#include "process.h"
#include "syscall-init.h"
#include "syscall.h"
#include "stdio-kernel.h"
#include "memory.h"
#include "fs.h"
#include "string.h"

void k_thread_a(void*);
void k_thread_b(void*);
void u_prog_a(void);
void u_prog_b(void);

int main(void) {
    put_str("I am kernel\n");
    init_all();
    intr_enable();
    printk("/dir1/subdir1 create %s!\n", sys_mkdir("/dir1/subdir1") == 0 ? "done" : "fail");
    printk("/dir1 create %s!\n", sys_mkdir("/dir1") == 0 ? "done" : "fail");
    printk("now, /dir1/subdir1 create %s!\n", sys_mkdir("/dir1/subdir1") == 0 ? "done" : "fail");
    int fd = sys_open("/dir1/subdir1/file2", O_CREATE | O_RDWR);
    if (fd != -1) {
        printk("/dir1/subdir1/file2 create done!\n"); 
        sys_write(fd, "Catch me if you can!\n", 21); 
        sys_lseek(fd, 0, SEEK_SET); 
        char buf[32] = {0}; 
        sys_read(fd, buf, 21); 
        printk("/dir1/subdir1/file2 says:\n%s", buf); 
        sys_close(fd);
    }
    // int32_t fd = sys_open("/file1", O_RDWR);
    // sys_write(fd, "hello,world", 11);
    // uint8_t buf[32] = {0};
    // printk("fd:%d\n", fd);
    // int read_bytes = sys_read(fd, buf, 32);
    // printk(" read %d bytes:\n", read_bytes);
    // sys_close(fd);
    // printk("%d closed now\n", fd);
    // printk("/file1 delete %s!\n", sys_unlink("/file1") == 0 ? "done" : "fail");
    while(1);
    return 0;
}
