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

void dir_list(struct dir *p_dir);

int main(void) {
    put_str("I am kernel\n");
    init_all();
    intr_enable();
    struct dir *p_dir = sys_opendir("/..");
    char buf[32] = {0};
    sys_getcwd(buf, 32);
    printk("cwd:%s\n", buf);
    sys_chdir("/dir1");
    sys_getcwd(buf, 32);
    printk("cwd:%s\n", buf);
    dir_close(p_dir);
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
