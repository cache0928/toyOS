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

void dir_list(struct dir *p_dir);

int main(void) {
    put_str("I am kernel\n");
    init_all();
    intr_enable();
    
    while(1);
    return 0;
}

void dir_list(struct dir *p_dir) {
    if (p_dir) {
        printk("content:\n");
        char* type = NULL;
        struct dir_entry* dir_e = NULL;
        while ((dir_e = sys_readdir(p_dir))) {
            if (dir_e->f_type == FT_REGULAR) {
                type = "regular";
            } else {
                type = "directory";
            }
            printk(" %s %s\n", type, dir_e->filename);
        }
        sys_rewinddir(p_dir);
    }
}

// init进程
void init() {
    // void *addr = malloc(256);
    uint32_t ret_pid = fork();
    if(ret_pid) {
        printf("i am father, my pid is %d, child pid is %d\n", getpid(), ret_pid);
        // free(addr);
    } else {
        printf("i am child, my pid is %d, ret pid is %d\n", getpid(), ret_pid);
        // free(addr);
    }
    void *addr1 = malloc(256);
    void *addr2 = malloc(512);
    void *addr3 = malloc(1);
    void *addr4 = malloc(128);
    void *addr5 = malloc(1024);
    free(addr1);
    free(addr2);
    free(addr3);
    free(addr4);
    free(addr5);
    while(1);
}
