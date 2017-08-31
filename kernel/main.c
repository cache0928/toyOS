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
    // process_execute(u_prog_a, "u_prog_a");
    // process_execute(u_prog_b, "u_prog_b");
    // thread_start("k_thread_a", 31, k_thread_a, "I am thread_a");
    // thread_start("k_thread_b", 31, k_thread_b, "I am thread_b");
    // thread_block(TASK_BLOCKED);
    
    // int32_t fd = sys_open("/file1", O_CREATE | O_RDWR);
    // int i = 0;
    // while (i++ < 50) {
    //     sys_write(fd, "hello,world", 11);
    // }
    // uint8_t buf[1024] = {0};
    // printk("fd:%d\n", fd);
    // sys_lseek(fd, -1, SEEK_END);
    // int read_bytes = sys_read(fd, buf, 768);
    // printk(" read %d bytes:\n", read_bytes);
    // sys_close(fd);
    // printk("%d closed now\n", fd);
    printk("/file1 delete %s!\n", sys_unlink("/file1") == 0 ? "done" : "fail");
    while(1);
    return 0;
}
