#include "process.h"
#include "global.h"
#include "memory.h"
#include "interrupt.h"
#include "debug.h"
#include "string.h"
#include "tss.h"
#include "console.h"

extern void intr_exit();

// 伪造中断环境，利用intr_exit来通过中断返回进入到用户进程
void start_process(void *filename_) {
    void *function = filename_;
    struct task_struct *cur = running_thread();
    cur->self_kstack += sizeof(struct thread_stack);
    struct intr_stack *proc_stack = (struct intr_stack *)cur->self_kstack;
    proc_stack->edi = proc_stack->esi = proc_stack->ebp = proc_stack->esp_dummy = 0;
    proc_stack->ebx = proc_stack->edx = proc_stack->ecx = proc_stack->eax = 0;
    proc_stack->gs = 0; // 用户态用不上
    proc_stack->ds = proc_stack->es = proc_stack->fs = SELECTOR_U_DATA;
    proc_stack->eip = function; // 入口
    proc_stack->cs = SELECTOR_U_CODE;
    proc_stack->eflags = EFLAGS_IOPL_0 | EFLAGS_MBS | EFLAGS_IF_1;
    // 用户池中申请一页充当用户态栈道，栈顶为0xc0000000
    proc_stack->esp = (void *)((uint32_t)get_a_page(PF_USER, USER_STACK3_VADDR) + PG_SIZE);
    proc_stack->ss = SELECTOR_U_DATA;
    asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (proc_stack) : "memory");
}

// 每次schedule的时候，都通过此方法加载新进程或者线程的页表，进程切换到它自己的页表，线程则换成内核页表
void page_dir_activate(struct task_struct *pthread) {
    // 默认内核页目录表地址
    uint32_t pagedir_phy_addr = 0x100000;
    if (pthread->pgdir != NULL) {
        // 是用户进程，pgdir在初始化进程的时候生成，用的是虚拟地址，所以要转换
        pagedir_phy_addr = addr_v2p((uint32_t)pthread->pgdir);
    }
    // 更新CR3来切换页表
    asm volatile ("movl %0, %%cr3" : : "r" (pagedir_phy_addr) : "memory");
}

// schedule时，直接用此方法来切换TSS中的esp0，以及更新CR3寄存器为对应进程的页表
void process_activate(struct task_struct *pthread) {
    ASSERT(pthread != NULL);
    // 更换页表
    page_dir_activate(pthread);
    // 更新TSS的esp0，如果切换过去的目标是线程，则不用更换了，因为线程用的栈本身就是0级栈
    if (pthread->pgdir) {
        update_tss_esp(pthread);
    }
}

// 为进程创建页目录表，将顶端256个pde拷贝过去，这样才能做到共享1gb内核空间
// 并将最顶端的pde指向目录表本身，便于之后修改、访问自己的页表
uint32_t *create_page_dir() {
    // 为了页表不让用户进程直接能访问到，所以在内核空间中申请
    uint32_t *page_dir_vaddr = get_kernel_pages(1);
    if (page_dir_vaddr == NULL) {
        console_put_str("create_page_dir: get_kernel_page failed!");
        return NULL;
    }
    // 复制头256项pde
    uint32_t *source = (uint32_t *)(0xfffff000 + 0x300 * 4); // 源目录表，即内核目录表
    uint32_t *destination = (uint32_t *)((uint32_t)page_dir_vaddr + 0x300 * 4);
    memcpy(destination, source, 1024);
    // 更新第1023个pde指向页目录表本身
    uint32_t new_page_dir_phy_addr = addr_v2p((uint32_t)page_dir_vaddr);
    page_dir_vaddr[1023] = new_page_dir_phy_addr | PG_US_U | PG_RW_W | PG_P_1;

    return page_dir_vaddr;
}

// 创建用户虚拟池的位图
void create_user_vaddr_bitmap(struct task_struct *user_prog) {
    user_prog->userprog_vaddr.vaddr_start = USER_VADDR_START;
    uint32_t bitmap_pg_cnt = DIV_ROUND_UP((0xc0000000 - USER_VADDR_START) / PG_SIZE / 8, PG_SIZE);
    // 位图放内核空间
    user_prog->userprog_vaddr.vaddr_bitmap.bits = get_kernel_pages(bitmap_pg_cnt);
    user_prog->userprog_vaddr.vaddr_bitmap.btmp_bytes_len = (0xc0000000 - USER_VADDR_START) / PG_SIZE / 8;

    bitmap_init(&user_prog->userprog_vaddr.vaddr_bitmap);
}

// 创建用户进程
void process_execute(void *filename, char *process_name) {
    // 创建进程PCB
    struct task_struct *thread = get_kernel_pages(1);
    init_thread(thread, process_name, default_prio);
    // 创建进程的虚拟池位图
    create_user_vaddr_bitmap(thread);
    // 布置首次switch_to调用start_process的环境
    thread_create(thread, start_process, filename);
    // 创建进程的页目录表
    thread->pgdir = create_page_dir();
    
    block_desc_init(thread->u_blcok_desc);

    // 将新进程加入到调度队列
    enum intr_status old_status = intr_disable();
    ASSERT(!elem_find(&thread_ready_list, &thread->general_tag));
    list_append(&thread_ready_list, &thread->general_tag);
    ASSERT(!elem_find(&thread_all_list, &thread->all_list_tag));
    list_append(&thread_all_list, &thread->all_list_tag);
    intr_set_status(old_status);
}