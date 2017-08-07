#include "memory.h"
#include "stdint.h"
#include "print.h"
#include "bitmap.h"
#include "string.h"
#include "debug.h"
#include "global.h"

#define PG_SIZE 4096

// 获取虚拟地址的目录表索引
#define PDE_IDX(addr) ((addr & 0xffc00000) >> 22)
// 获取虚拟地址的页表索引
#define PTE_IDX(addr) ((addr & 0x003ff000) >> 12)
// 获取虚拟地址对应的PTE指针
uint32_t *pte_ptr(uint32_t vaddr) {
    uint32_t *pte = (uint32_t *)(0xffc00000 + ((vaddr & 0xffc00000) >> 10) + PTE_IDX(vaddr) * 4);
    return pte;
}
// 获取虚拟地址对应的PDE指针
uint32_t *pde_ptr(uint32_t vaddr) {
    uint32_t *pde = (uint32_t *)((0xfffff000) + PDE_IDX(vaddr) * 4);
    return pde;
}

// 内核主线程栈顶为0xc009f000
// 设计上0xc009e000 - 0xc009efff 为主线程PCB
// 一个页框对应128M内存，设计为0xc009a000 - 0xc009dfff共4页来存放各种内存池的位图
#define MEM_BITMAP_BASE 0xc009a000

// 内核堆的起始地址, 跨过低端1mb连续
#define K_HEAP_START 0xc0100000

// 物理内存池结构
struct pool {
    struct bitmap pool_bitmap; // 物理内存池的位图
    uint32_t phy_addr_start; // 物理内存池的起始地址
    uint32_t pool_size; // 物理内存池的字节容量
};

struct pool kernel_pool, user_pool;
struct virtual_addr kernel_vaddr;

// 在pf表示的虚拟内存池中申请连续pg_cnt个虚拟页，成功返回虚拟页的起始地址，失败返回NULL
static void *vaddr_get(enum pool_flags pf, uint32_t pg_cnt) {
    int vaddr_start = 0;
    int bit_idx_start = -1;
    uint32_t cnt = 0;
    if (pf == PF_KERNEL) {
        // 内核虚拟池
        bit_idx_start = bitmap_scan(&kernel_vaddr.vaddr_bitmap, pg_cnt);
        if (bit_idx_start == -1) {
            return NULL;
        }
        while (cnt < pg_cnt) {
            // 位图中标记这些页已经使用
            bitmap_set(&kernel_vaddr.vaddr_bitmap, bit_idx_start + cnt++, 1);
        }
        vaddr_start = kernel_vaddr.vaddr_start + bit_idx_start * PG_SIZE;
    } else {
        // 用户态程序虚拟池
    }
    return (void *)vaddr_start;
}

// 在m_pool指向的物理池中分配一个物理页，成功返回页框的物理地址，失败则返回NULL
static void *palloc(struct pool *m_pool) {
    int bit_idx = bitmap_scan(&m_pool->pool_bitmap, 1);
    if (bit_idx == -1) {
        return NULL;
    }
    // 在位图中标记该页已使用
    bitmap_set(&m_pool->pool_bitmap, bit_idx, 1);
    uint32_t page_phyaddr = ((bit_idx * PG_SIZE) + m_pool->phy_addr_start);
    return (void *)page_phyaddr;
}

// 关联某个虚拟地址和物理地址，即在虚拟地址对应的PTE中填写物理地址和一些属性
static void page_table_add(void *_vaddr, void *_page_phyaddr) {
    uint32_t vaddr = (uint32_t)_vaddr;
    uint32_t page_phyaddr = (uint32_t)_page_phyaddr;
    uint32_t *pde = pde_ptr(vaddr);
    uint32_t *pte = pte_ptr(vaddr);
    // 填表的步骤：
    // 先查看虚拟地址对应的页表在不在内存中，即看pde指向的内容的P位是不是1
    // 如果页表不在内存中，在内核的物理池中申请一个页框来放页表
    // 查看虚拟地址对应的物理页在不在内存中，即看pte指向的内容的P位是不是1
    // 新申请的物理地址一般肯定还没有关联到页表中的
    // 如果不是1，即代表该物理页还不在内存中，因此可以关联
    // 就将PTE填写成与该物理页相关的数据
    if (*pde & 0x00000001) {
        ASSERT(!(*pte & 0x00000001));
        if (!(*pte & 0x00000001)) {
            *pte = (page_phyaddr | PG_US_U | PG_RW_W | PG_P_1);
        } else {
            PANIC("pte repeat");
            *pte = (page_phyaddr | PG_US_U | PG_RW_W | PG_P_1);
        }
    } else {
        // 页表都还不存在，因此要先创页表，再对物理页进行映射
        uint32_t pde_phyaddr = (uint32_t)palloc(&kernel_pool);
        *pde = (pde_phyaddr | PG_US_U | PG_RW_W | PG_P_1);
        // 新的页表清零，避免有些脏数据
        memset((void *)((int)pte & 0xfffff000), 0, PG_SIZE);
        ASSERT(!(*pte & 0x00000001));
        *pte = (page_phyaddr | PG_US_U | PG_RW_W | PG_P_1);
    }
}

// 请求分配pg_cnt个页，成功返回起始虚拟地址，失败返回NULL
void *malloc_page(enum pool_flags pf, uint32_t pg_cnt) {
    // 请求分配的内存要大于0，小于物理池上限
    ASSERT(pg_cnt > 0 && pg_cnt < 3840);
    // 请求分配内存的过程：
    // 1. 通过vaddr_get在虚拟池中申请连续的虚拟页
    // 2. 通过palloc在物理池中申请物理页（不一定要连续）
    // 3. 通过page_table_add将以上的虚拟页和物理页进行关联
    void *vaddr_start = vaddr_get(pf, pg_cnt);
    if (vaddr_start == NULL) {
        return NULL;
    }
    uint32_t vaddr = (uint32_t)vaddr_start, cnt = pg_cnt;
    struct pool *mem_pool = pf & PF_KERNEL ? &kernel_pool : &user_pool;
    // 虚拟地址连续，而物理地址可以不连续，所以逐个映射
    while (cnt-- > 0) {
        void *page_phyaddr = palloc(mem_pool);
        if (page_phyaddr == NULL) {
            // 这里还差回滚，因为失败的话其实位图已经被改了，需要改回去
            return NULL;
        }
        page_table_add((void *)vaddr, page_phyaddr); // 关联虚拟页和物理页
        vaddr += PG_SIZE;
    }
    return vaddr_start;
}

// 在内核物理池中申请pg_cnt个页，成功返回虚拟页起始地址，失败返回NULL
void *get_kernel_pages(uint32_t pg_cnt) {
    void *vaddr = malloc_page(PF_KERNEL, pg_cnt);
    if (vaddr != NULL) {
        // 将申请到的物理空间全部清0
        memset(vaddr, 0, pg_cnt * PG_SIZE);
    }
    return vaddr;
}

// 初始化内存池，参数为总物理内存的字节容量
static void mem_pool_init(uint32_t all_mem) {
    put_str("   mem_pool_init start\n");
    // 已经占用了空间的页表：1个目录表+对应内核的255个页表
    uint32_t page_table_size = PG_SIZE * 256;
    // 已使用的物理内存大小，已占空间的页表+低端1mb
    uint32_t used_mem = page_table_size + 0x100000;
    // 可用物理内存
    uint32_t free_mem = all_mem - used_mem;
    // 可用物理内存对应的页数, 不足4k的直接忽略不要了
    uint16_t all_free_pages = free_mem / PG_SIZE;
    // 分给内核的物理页数量
    uint16_t kernel_free_pages = all_free_pages / 2;
    // 分给用户态程序的物理页数量
    uint16_t user_free_pages = all_free_pages - kernel_free_pages;

    // 内核物理池位图的字节容量，位图中1位表示1页
    uint32_t kbm_length = kernel_free_pages / 8;
    // 用户态程序物理池的字节容量
    uint32_t ubm_length = user_free_pages / 8;
    // 内核物理池的起始地址直接从紧贴着低端1mb+已占空间的页表的地址之后开始
    uint32_t kp_start = used_mem;
    // 用户物理池紧贴着内核物理池
    uint32_t up_start = kp_start + kernel_free_pages * PG_SIZE;

    kernel_pool.phy_addr_start = kp_start;
    user_pool.phy_addr_start = up_start;
    kernel_pool.pool_size = kernel_free_pages * PG_SIZE;
    user_pool.pool_size = user_free_pages * PG_SIZE;
    kernel_pool.pool_bitmap.btmp_bytes_len = kbm_length;
    user_pool.pool_bitmap.btmp_bytes_len = ubm_length;
    // 设计上从0xc009a000开始部署位图
    kernel_pool.pool_bitmap.bits = (void *)MEM_BITMAP_BASE;
    user_pool.pool_bitmap.bits = (void *)(MEM_BITMAP_BASE + kbm_length);
    
    put_str("      kernel_pool_bitmap_start:");
    put_int((int)kernel_pool.pool_bitmap.bits);
    put_str(" kernel_pool_phy_addr_start:");
    put_int(kernel_pool.phy_addr_start);
    put_char('\n');
    put_str("      user_pool_bitmap_start:");
    put_int((int)user_pool.pool_bitmap.bits);
    put_str(" user_pool_phy_addr_start:");
    put_int(user_pool.phy_addr_start);
    put_char('\n');

    // 将位图置0
    bitmap_init(&kernel_pool.pool_bitmap);
    bitmap_init(&user_pool.pool_bitmap);

    // 设置内核虚拟池位图
    kernel_vaddr.vaddr_bitmap.btmp_bytes_len = kbm_length;
    kernel_vaddr.vaddr_bitmap.bits = (void *)(MEM_BITMAP_BASE + kbm_length + ubm_length);
    kernel_vaddr.vaddr_start = K_HEAP_START;
    bitmap_init(&kernel_vaddr.vaddr_bitmap);

    put_str("   mem_pool_init done\n");
}

void mem_init() {
    put_str("mem_init_start\n");
    // 在bootloader里保存了获取到的物理内存大小到0xb00
    uint32_t mem_bytes_total = (*(uint32_t *)(0xb00));
    mem_pool_init(mem_bytes_total);
    put_str("mem_init done\n");
}
