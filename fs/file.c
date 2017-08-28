#include "file.h"
#include "fs.h"
#include "super_block.h"
#include "inode.h"
#include "stdio-kernel.h"
#include "memory.h"
#include "debug.h"
#include "interrupt.h"
#include "string.h"
#include "thread.h"
#include "global.h"

#define DEFAULT_SECS 1

struct file file_table[MAX_FILE_OPEN];

// 从文件表 file_table中获取一个空闲位，成功返回下标，失败返回-1
int32_t get_free_slot_in_global() {
    // 跨过标准输入输出错误
    uint32_t fd_idx = 3;
    while (fd_idx < MAX_FILE_OPEN) {
        if (file_table[fd_idx].fd_inode == NULL) {
            // 还没分配inode说明是空闲的
            break;
        }
        fd_idx++;
    }
    if (fd_idx == MAX_FILE_OPEN) {
        // 无空闲位
        printk("exceed max open files\n");
        return -1;
    }
    return fd_idx;
}

// 将从文件表中获取的空闲位下标放入自己PCB的文件描述符数组中，成功返回文件描述符数组下标，失败返回-1
int32_t pcb_fd_install(int32_t global_fd_idx) {
    struct task_struct *cur = running_thread();
    uint8_t local_fd_idx = 3; // 跨过标准输入输出错误
    while (local_fd_idx < MAX_FILES_OPEN_PER_PROC) {
        if (cur->fd_table[local_fd_idx] == -1) {
            // 说明是空闲的，可以使用
            cur->fd_table[local_fd_idx] = global_fd_idx;
            break;
        }
        local_fd_idx++;
    }
    if (local_fd_idx == MAX_FILES_OPEN_PER_PROC) {
        printk("exceed max open files_per_proc\n");
        return -1;
    }
    return local_fd_idx;
}

// 分配一个新的inode，返回inode序号
int32_t inode_bitmap_alloc(struct partition *part) {
    int32_t bit_idx = bitmap_scan(&part->inode_bitmap, 1);
    if (bit_idx == -1) {
        return -1;
    }
    bitmap_set(&part->inode_bitmap, bit_idx, 1);
    return bit_idx;
 }

// 分配一个空闲扇区，返回扇区编号
int32_t block_bitmap_alloc(struct partition* part) {
    int32_t bit_idx = bitmap_scan(&part->block_bitmap, 1);
    if (bit_idx == -1) {
        return -1;
    }
    bitmap_set(&part->block_bitmap, bit_idx, 1);
    return (part->sb->data_start_lba + bit_idx);
}

// 将内存中的bitmap第bit_idx位所在的扇区同步回硬盘
void bitmap_sync(struct partition *part, uint32_t bit_idx, uint8_t btmp_type) {
    // 所在的扇区相对于位图所有扇区的偏移lba
    uint32_t off_sec = bit_idx / BITS_PER_SECTOR;
    // 把上面的偏移lba换算成偏移字节
    uint32_t off_size = off_sec * BLOCK_SIZE;

    uint32_t sec_lba;
    uint8_t *bitmap_off;

    switch (btmp_type) {
        case INODE_BITMAP:
            // 硬盘上的目标扇区
            sec_lba = part->sb->inode_bitmap_lba + off_sec;
            // 内存上的位图源数据
            bitmap_off = part->inode_bitmap.bits + off_size;
            break;
        case BLOCK_BITMAP:
            sec_lba = part->sb->block_bitmap_lba + off_sec;
            bitmap_off = part->block_bitmap.bits + off_size;
            break;
    }
    ide_write(part->my_disk, sec_lba, bitmap_off, 1);
}

// 创建文件，成功返回文件描述符，失败返回-1
int32_t file_create(struct dir *parent_dir, char *filename, uint8_t flag) {
    // 创建的步骤
    // 1.为新文件在inode位图中分配inode编号，并在内核空间申请inode
    // 2.将inode编号写入全局文件表，并得到全局文件表的相应索引
    // 3.为新文件新建目录项，写入到父目录的目录项中
    // 4.将上面三步可能改变的父目录的inode、新文件的inode、inode位图同步到磁盘
    // 5.将所得的全局文件表索引写入到当前进程pcb的文件描述符表
    void *io_buf = sys_malloc(1024);
    if (io_buf == NULL) {
        printk("in file_creat: sys_malloc for io_buf failed\n");
        return -1;
    }
    uint8_t rollback_step = 0; // 回滚标记
    // 为新文件分配inode编号
    int32_t inode_no = inode_bitmap_alloc(cur_part);
    if (inode_no == -1) {
        printk("in file_creat: allocate inode failed\n");
        return -1;
    }
    // 在内核堆中为inode申请内存
    struct task_struct *cur = running_thread();
    uint32_t *cur_pagedir_bak = cur->pgdir;
    cur->pgdir = NULL;
    struct inode *new_file_inode = (struct inode *)sys_malloc(sizeof(struct inode));
    cur->pgdir = cur_pagedir_bak;
    if (new_file_inode == NULL) {
        printk("file_create: sys_malloc for inode failded\n");
        rollback_step = 1;
        goto rollback;
    }
    inode_init(inode_no, new_file_inode);
    // 在全局文件表中找空位写入新的inode编号
    int fd_idx = get_free_slot_in_global();
    if (fd_idx == -1) {
        printk("exceed max open files\n");
        rollback_step = 2;
        goto rollback;
    }
    file_table[fd_idx].fd_inode = new_file_inode;
    file_table[fd_idx].fd_pos = 0;
    file_table[fd_idx].fd_flag = flag;
    file_table[fd_idx].fd_inode->write_deny = false;
    // 为新文件新建目录项，并写入到父目录的目录项表中
    struct dir_entry new_dir_entry;
    memset(&new_dir_entry, 0, sizeof(struct dir_entry));
    create_dir_entry(filename, inode_no, FT_REGULAR, &new_dir_entry);
    if (!sync_dir_entry(parent_dir, &new_dir_entry, io_buf)) {
        printk("sync dir_entry to disk failed\n");
        rollback_step = 3;
        goto rollback;
    }
    // 将可能修改的东西同步到硬盘
    memset(io_buf, 0, 1024);
    inode_sync(cur_part, parent_dir->inode, io_buf);
    memset(io_buf, 0, 1024);
    inode_sync(cur_part, new_file_inode, io_buf);
    bitmap_sync(cur_part, inode_no, INODE_BITMAP);
    // 将新文件的inode加入到当前分区的inode队列
    list_push(&cur_part->open_inodes, &new_file_inode->inode_tag);
    new_file_inode->i_open_cnts = 1;

    sys_free(io_buf);
    // 全局文件表索引填入到pcb的文件描述符表
    return pcb_fd_install(fd_idx);

rollback:
    // 出错回滚
    switch (rollback_step) {
        case 3:
            memset(&file_table[fd_idx], 0, sizeof(struct file));
        case 2:
            sys_free(new_file_inode);
        case 1:
            bitmap_set(&cur_part->inode_bitmap, inode_no, 0);
            break;
    }
    sys_free(io_buf);
    return -1;
}