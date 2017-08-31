#ifndef __THREAD_THREAD_H
#define __THREAD_THREAD_H
#include "stdint.h"
#include "list.h"
#include "memory.h"
// 线程函数的形参
typedef void (*thread_func)(void *);
typedef uint16_t pid_t;
// 线程／进程的状态
enum task_status {
    TASK_RUNNING,
    TASK_READY,
    TASK_BLOCKED,
    TASK_WAITING,
    TASK_HANGING,
    TASK_DIED
};

// 中断栈，线程在中断发生时将按此结构入栈保护上下文
struct intr_stack {
    uint32_t vec_no;
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t esp_dummy;
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;
    uint32_t gs;
    uint32_t fs;
    uint32_t es;
    uint32_t ds;
    // 低特权级进入高特权级时还会压入
    uint32_t err_code; // 错误号
    void (*eip)();
    uint32_t cs;
    uint32_t eflags;
    void *esp;
    uint32_t ss;
};

// 线程栈，保存线程中运行的函数地址
// 以及备份ABI规定的，函数调用前和返回后的ebp、ebx、edi、esi和esp值不变
struct thread_stack {
    uint32_t ebp;
    uint32_t ebx;
    uint32_t edi;
    uint32_t esi;
    // 线程第一次执行时，指向待调用的函数kernel_thread，其余时候指向switch_to的返回地址
    void (*eip)(thread_func func, void *func_arg);
    // 以下仅在线程第一次被调度上CPU时使用
    // unused_retaddr只是占位的返回地址，因为kernel_thread是通过ret而不是call来调用，因此这个
    // 值只是为了给kernel_thread在栈中检索自己参数的时候提供了一个基址，类似ebp的作用
    void *unused_retaddr;
    thread_func function;
    void *func_arg;
};

// 每个进程同时打开的最多文件数
#define MAX_FILES_OPEN_PER_PROC 8
// PCB结构
struct task_struct {
    uint32_t *self_kstack; // 各线程自己的内核栈栈顶
    pid_t pid;
    enum task_status status;
    uint8_t priority; // 优先级
    char name[16];
    uint8_t ticks; // 每次在处理器上执行的时钟周期
    uint32_t elapsed_ticks; // 任务已经执行了多少个时钟周期
    uint32_t fd_table[MAX_FILES_OPEN_PER_PROC]; // 文件描述符数组
    // 在就绪队列中的节点标记
    struct list_elem general_tag;
    // 在所有队列中的节点tag
    struct list_elem all_list_tag;

    uint32_t *pgdir; // 页表的虚拟地址 
    struct virtual_addr userprog_vaddr; // 用户进程的虚拟内存池
    struct mem_block_desc u_blcok_desc[DESC_CNT]; // 用户进程的内存块描述符
    uint32_t cwd_inode_nr; // 进程当前所在的工作目录的inode号
    uint32_t stack_magic; // 栈的边界标记，用于判断栈是否溢出，魔数
};
struct task_struct *thread_start(char *name, int prio, thread_func function, void *func_arg);
struct task_struct *running_thread();
void schedule();
void thread_init();
void thread_block(enum task_status stat);
void thread_yield();
void thread_unblock(struct task_struct *pthread);
void thread_create(struct task_struct *pthread, thread_func function, void *func_arg);
void init_thread(struct task_struct *pthread, char *name, int prio);
extern struct list thread_ready_list; // 就绪队列
extern struct list thread_all_list; // 所有任务队列
#endif