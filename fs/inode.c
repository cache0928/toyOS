#include "inode.h"
#include "fs.h"
#include "file.h"
#include "global.h"
#include "debug.h"
#include "memory.h"
#include "interrupt.h"
#include "list.h"
#include "stdio-kernel.h"
#include "string.h"
#include "super_block.h"
#include "thread.h"
#include "ide.h"
// inode的位置
struct inode_position {
    bool two_sec; // 是否横跨2个扇区
    uint32_t sec_lba; // inode所在的扇区
    uint32_t off_size; // inode在扇区中的字节偏移
};

// 获取inode所在的位置（所在的扇区以及扇区内的偏移）
static void inode_locate(struct partition *part, uint32_t inode_no, struct inode_position *inode_pos) {
    ASSERT(inode_no < 4096);
    uint32_t inode_table_lba = part->sb->inode_table_lba;

    uint32_t inode_size = sizeof(struct inode);
    uint32_t off_size = inode_no * inode_size;
    // 相对于inode表起始位置的扇区偏移量和扇区内偏移量
    uint32_t off_sec = off_size / SECTOR_SIZE;
    uint32_t off_size_in_sec = off_size % SECTOR_SIZE;

    // inode是否横跨两个扇区
    uint32_t left_in_sec = SECTOR_SIZE - off_size_in_sec;
    if (left_in_sec < inode_size) {
        // 在扇区的偏移位置之后的空间放不下一个inode，所以是横跨两个扇区
        inode_pos->two_sec = true;
    } else {
        inode_pos->two_sec = false;
    }

    inode_pos->sec_lba = inode_table_lba + off_sec;
    inode_pos->off_size = off_size_in_sec;
}

// 将inode写入到磁盘分区
void inode_sync(struct partition *part, struct inode *inode, void *io_buf) {
    uint32_t inode_no = inode->i_no;
    struct inode_position inode_pos;
    inode_locate(part, inode_no, &inode_pos);
    ASSERT(inode_pos.sec_lba <= (part->start_lba + part->sec_cnt));

    struct inode pure_inode;
    memcpy(&pure_inode, inode, sizeof(struct inode));
    // 下面三个属性只有在内存中有意义，写会磁盘的时候要恢复成默认值
    pure_inode.i_open_cnts = 0;
    pure_inode.write_deny = false;
    pure_inode.inode_tag.prev = pure_inode.inode_tag.next = NULL;

    char *inode_buf = (char *)io_buf;
    if (inode_pos.two_sec) {
        // 如果横跨两个扇区，则要把两个扇区的内容都读出来，改掉inode的部分，再写回去
        ide_read(part->my_disk, inode_pos.sec_lba, inode_buf, 2);
        memcpy(inode_buf + inode_pos.off_size, &pure_inode, sizeof(struct inode));
        ide_write(part->my_disk, inode_pos.sec_lba, inode_buf, 2);
    } else {
        ide_read(part->my_disk, inode_pos.sec_lba, inode_buf, 1);
        memcpy(inode_buf + inode_pos.off_size, &pure_inode, sizeof(struct inode));
        ide_write(part->my_disk, inode_pos.sec_lba, inode_buf, 1);
    }
}

// 根据inode编号打开inode，即将相应的inode加载到内存中
struct inode *inode_open(struct partition *part, uint32_t inode_no) {
    // 先在已打开的inode队列中找
    struct list_elem *elem = part->open_inodes.head.next;
    struct inode *inode_found;
    while (elem != &part->open_inodes.tail) {
        inode_found = elem2entry(struct inode, inode_tag, elem);
        if (inode_found->i_no == inode_no) {
            // 找到的话，就把open次数+1
            inode_found->i_open_cnts++;
            return inode_found;
        }
        elem = elem->next;
    }

    // 如果队列中没有，则要去磁盘中加载
    struct inode_position inode_pos;
    inode_locate(part, inode_no, &inode_pos);

    struct task_struct *cur = running_thread();
    // 为了所有进程能共享打开的inode，所以inode要分配在内核空间中
    // sys_malloc是通过判断PCB的pgdir是不是NULL来决定是在内核空间还是用户空间中分配内存的
    // 为了在内核空间中分配，先把pgdir置NULL，然后分配完再改回来
    uint32_t *cur_pagedir_bak = cur->pgdir;
    cur->pgdir = NULL;
    inode_found = (struct inode *)sys_malloc(sizeof(struct inode));
    cur->pgdir = cur_pagedir_bak;

    char *inode_buf;
    if (inode_pos.two_sec) {
        // 横跨两个扇区
        inode_buf = (char *)sys_malloc(2 * SECTOR_SIZE);
        ide_read(part->my_disk, inode_pos.sec_lba, inode_buf, 2);
    } else {
        inode_buf = (char *)sys_malloc(SECTOR_SIZE);
        ide_read(part->my_disk, inode_pos.sec_lba, inode_buf, 1);
    }
    memcpy(inode_found, inode_buf + inode_pos.off_size, sizeof(struct inode));
    // 打开之后插入到open队列
    list_push(&part->open_inodes, &inode_found->inode_tag);
    inode_found->i_open_cnts = 1;

    sys_free(inode_buf);
    return inode_found;
}

// 关闭inode，减少inode的open数量，如果为0了，则在内存中释放inode
void inode_close(struct inode* inode) {
    enum intr_status old_status = intr_disable();
    if (--inode->i_open_cnts == 0) {
        list_remove(&inode->inode_tag);
        // 确保释放内核空间中的inode
        struct task_struct* cur = running_thread();
        uint32_t* cur_pagedir_bak = cur->pgdir;
        cur->pgdir = NULL;

        sys_free(inode);
        cur->pgdir = cur_pagedir_bak;
    }
    intr_set_status(old_status);
}

// 初始化一个新的inode
void inode_init(uint32_t inode_no, struct inode *new_inode) {
    new_inode->i_no = inode_no;
    new_inode->i_size = 0;
    new_inode->i_open_cnts = 0;
    new_inode->write_deny = false;
    // 初始化sectors数组，全部初始化为0
    uint16_t sec_idx = 0;
    while (sec_idx < 13) {
        new_inode->i_sectors[sec_idx] = 0;
        sec_idx++;
    }
}