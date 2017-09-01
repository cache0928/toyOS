#ifndef __KERNEL_MEMORY_H
#define __KERNEL_MEMORY_H
#include "stdint.h"
#include "bitmap.h"
#include "list.h"
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
void *get_user_pages(uint32_t pg_cnt);
uint32_t addr_v2p(uint32_t vaddr);
void *get_a_page(enum pool_flags pf, uint32_t vaddr);

/** 用于小内存分配时的数据结构 **/

// 堆里的内存块
struct mem_block {
    struct list_elem free_elem;
};

// 内存块描述符，一共定义16 - 1024 7种大小，大于1024的直接按页分配
#define DESC_CNT 7

struct mem_block_desc {
    uint32_t block_size; // 内存块大小
    uint32_t blocks_per_arena; // 这个类型arena可容纳block_size尺寸的内存块的数量
    struct list free_list; // 目前可用的mem_block链表
};
void block_desc_init(struct mem_block_desc *desc_array);
void *sys_malloc(uint32_t size);
void sys_free(void *ptr);
void mfree_page(enum pool_flags pf, void *_vaddr, uint32_t pg_cnt);
void *get_a_page_without_opvaddrbitmap(enum pool_flags pf, uint32_t vaddr);
#endif