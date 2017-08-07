#ifndef __KERNEL_MEMORY_H
#define __KERNEL_MEMORY_H
#include "stdint.h"
#include "bitmap.h"
// 物理池标记
enum pool_flags {
    PF_KERNEL = 1,
    PF_USER = 2
};
// 页表项或目录项的一些属性
#define PG_P_1 1
#define PG_P_0 0
#define PG_RW_R 0
#define PG_RW_W 2
#define PG_US_S 0
#define PG_US_U 4
// 虚拟地址池
struct virtual_addr {
    struct bitmap vaddr_bitmap; // 虚拟地址对应的位图
    uint32_t vaddr_start; // 虚拟地址起始地址
};
// 内核和用户的物理地址池
extern struct pool kernel_pool, user_pool;
void mem_init();
void *get_kernel_pages(uint32_t pg_cnt);
void *malloc_page(enum pool_flags pf, uint32_t pg_cnt);
#endif