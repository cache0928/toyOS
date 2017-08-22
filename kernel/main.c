#include "print.h"
#include "init.h"
#include "thread.h"
#include "interrupt.h"
#include "console.h"
#include "process.h"
#include "stdio.h"
#include "syscall.h"
#include "syscall-init.h"
#include "memory.h"

void k_thread_a(void *);
void k_thread_b(void *);
void u_prog_a();

int main(void) {
    put_str("I am kernel\n");
    init_all();

    // process_execute(u_prog_a, "user_prog_a");
    thread_start("k_thread_a", 31, k_thread_a, "argA ");
    thread_start("k_thread_b", 31, k_thread_b, "argA ");
    intr_enable();
    
    while(1);
    return 0;
}

void k_thread_a(void *arg) {     
    char *para = (char *)arg;
    void *addr = sys_malloc(33);
    console_put_str(" I am thread_a, sys_malloc(33), addr is 0x");
    console_put_int((int)addr);
    console_put_char('\n');
    while(1);
}

void k_thread_b(void *arg) {     
    char *para = (char *)arg;
    void *addr = sys_malloc(63);
    console_put_str(" I am thread_a, sys_malloc(63), addr is 0x");
    console_put_int((int)addr);
    console_put_char('\n');
    while(1);
}

void u_prog_a() {
    char *name = "prog_a";
    uint32_t prog_a_pid = getpid();
    printf(" %s_pid:0x%x%c", name, prog_a_pid, '\n');
    while(1);
}

