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
#include "dir.h"

void k_thread_a(void*);
void k_thread_b(void*);
void u_prog_a(void);
void u_prog_b(void);

int main(void) {
    put_str("I am kernel\n");
    init_all();
    intr_enable();
    struct dir *p_dir = sys_opendir("/..");
    if (p_dir) {
        printk("/dir1/subdir1 open done!\ncontent:\n");
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
        if (sys_closedir(p_dir) == 0) { 
            printk("/dir1/subdir1 close done!\n"); 
        } else { 
            printk("/dir1/subdir1 close fail!\n"); 
        }
    } else {
        printk("/dir1/subdir1 open fail!\n");
    }
    while(1);
    return 0;
}
