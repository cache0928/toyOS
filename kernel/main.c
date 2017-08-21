#include "print.h"
#include "init.h"
#include "thread.h"
#include "interrupt.h"
#include "console.h"
#include "process.h"
#include "stdio.h"
#include "syscall.h"
#include "syscall-init.h"

void k_thread_a(void *);
void u_prog_a();

int main(void) {
    put_str("I am kernel\n");
    init_all();

    process_execute(u_prog_a, "user_prog_a");
    thread_start("k_thread_a", 31, k_thread_a, "argA ");
    intr_enable();
    
    while(1);
    return 0;
}

void k_thread_a(void *arg) {     
    char* para = arg;
    console_put_str(" thread_a_pid:0x");
    console_put_int(sys_getpid());
    console_put_char('\n');
    while(1);
}

void u_prog_a() {
    char *name = "prog_a";
    uint32_t prog_a_pid = getpid();
    printf(" %s_pid:0x%x%c", name, prog_a_pid, '\n');
    while(1);
}

