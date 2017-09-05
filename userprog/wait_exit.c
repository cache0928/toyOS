#include "wait_exit.h"
#include "global.h"
#include "debug.h"
#include "thread.h"
#include "list.h"
#include "stdio-kernel.h"
#include "memory.h"
#include "bitmap.h"
#include "fs.h"

/* 释放用户进程资源: 
 * 1 页表中对应的物理页
 * 2 虚拟内存池占物理页框
 * 3 关闭打开的文件 */
static void release_prog_resource(struct task_struct *release_thread) {
    uint32_t *pgdir_vaddr = release_thread->pgdir;
    uint16_t user_pde_nr = 768, pde_idx = 0;
    uint32_t pde = 0;
    uint32_t *v_pde_ptr = NULL;

    uint16_t user_pte_nr = 1024, pte_idx = 0;
    uint32_t pte = 0;
    uint32_t *v_pte_ptr = NULL;

    uint32_t *first_pte_vaddr_in_pde = NULL;
    uint32_t pg_phy_addr = 0;

    // 回收除了目录表之外的所有页表
    while (pde_idx < user_pde_nr) {
        v_pde_ptr = pgdir_vaddr + pde_idx;
        pde = *v_pde_ptr;
        if (pde & 0x00000001) {
            // 一个目录项对应4m空间，依次排布，所以可以根据是第几个目录选项来反算出对应的是哪一段4m空间
            // 进而算出这个4m空间对应的那个页表的起始虚拟地址
            first_pte_vaddr_in_pde = pte_ptr(pde_idx * 0x400000);
            pte_idx = 0;
            // 遍历这个页表
            while (pte_idx < user_pte_nr) {
                v_pte_ptr = first_pte_vaddr_in_pde + pte_idx;
                pte = *v_pte_ptr;
                if (pte & 0x00000001) {
                    // 回收物理页
                    pg_phy_addr = pte & 0xfffff000;
                    free_a_phy_page(pg_phy_addr);
                }
                pte_idx++;
            }
            // 回收这个页表的空间
            pg_phy_addr = pde & 0xfffff000;
            free_a_phy_page(pg_phy_addr);
        }
        pde_idx++;
    }

    // 回收用户虚拟地址池所占的物理内存
    uint32_t bitmap_pg_cnt = (release_thread->userprog_vaddr.vaddr_bitmap.btmp_bytes_len) / PG_SIZE;
    uint8_t *user_vaddr_pool_bitmap = release_thread->userprog_vaddr.vaddr_bitmap.bits;
    mfree_page(PF_KERNEL, user_vaddr_pool_bitmap, bitmap_pg_cnt);

    // 关闭进程打开的文件 
    uint8_t fd_idx = 3;
    while(fd_idx < MAX_FILES_OPEN_PER_PROC) {
        if (release_thread->fd_table[fd_idx] != -1) {
            sys_close(fd_idx);
        }
        fd_idx++;
    }
}

/* list_traversal的回调函数,
 * 查找pelem的parent_pid是否是ppid,成功返回true,失败则返回false */
static bool find_child(struct list_elem *pelem, int32_t ppid) {
    struct task_struct *pthread = elem2entry(struct task_struct, all_list_tag, pelem);
    if (pthread->parent_pid == ppid) {
        return true;
    }
    return false;
}

/* list_traversal的回调函数,
 * 查找状态为TASK_HANGING的任务 */
static bool find_hanging_child(struct list_elem *pelem, int32_t ppid) {
    struct task_struct *pthread = elem2entry(struct task_struct, all_list_tag, pelem);
    if (pthread->parent_pid == ppid && pthread->status == TASK_HANGING) {
        return true;
    }
    return false; 
}

/* list_traversal的回调函数,
 * 将一个子进程过继给init */
static bool init_adopt_a_child(struct list_elem *pelem, int32_t pid) {
    struct task_struct* pthread = elem2entry(struct task_struct, all_list_tag, pelem);
    if (pthread->parent_pid == pid) {
        pthread->parent_pid = 1;
    }
    return false;
}

/* 等待子进程调用exit,将子进程的退出状态保存到status指向的变量.
 * 成功则返回子进程的pid,失败则返回-1 */
pid_t sys_wait(int32_t *status) {
    struct task_struct* parent_thread = running_thread();

    while(1) {
        struct list_elem *child_elem = list_traversal(&thread_all_list, find_hanging_child, parent_thread->pid);
        if (child_elem != NULL) {
            // 有挂起的子进程
            struct task_struct* child_thread = elem2entry(struct task_struct, all_list_tag, child_elem);
            *status = child_thread->exit_status; 
            uint16_t child_pid = child_thread->pid;

            // 回收子进程的pcb和页目录表，并从调度队列中删除
            thread_exit(child_thread, false); // 传入false,使thread_exit调用后回到此处
            return child_pid;
        } 

        // 如果没有挂起的子进程，如果没有，先判断是否有子进程，如果没有就直接返回-1，如果有就阻塞自己等待子进程唤醒
        child_elem = list_traversal(&thread_all_list, find_child, parent_thread->pid);
        if (child_elem == NULL) {
            return -1;
        } else {
            thread_block(TASK_WAITING); 
        }
    }
}

/* 子进程用来结束自己时调用，main函数返回时CRT会调用这个函数，传入main的返回值 */
void sys_exit(int32_t status) {
    struct task_struct *child_thread = running_thread();
    child_thread->exit_status = status; 
    if (child_thread->parent_pid == -1) {
        PANIC("sys_exit: child_thread->parent_pid is -1\n");
    }

    // 将进程child_thread的所有子进程都过继给init
    list_traversal(&thread_all_list, init_adopt_a_child, child_thread->pid);

    // 回收进程child_thread的资源，除了页目录表和pcb外全被回收
    release_prog_resource(child_thread); 

    // 如果父进程正在等待子进程退出,将父进程唤醒 
    struct task_struct *parent_thread = pid2thread(child_thread->parent_pid);
    if (parent_thread->status == TASK_WAITING) {
        thread_unblock(parent_thread);
    }

    // 将自己挂起,等待父进程获取其status,并回收其pcb
    thread_block(TASK_HANGING);
}
