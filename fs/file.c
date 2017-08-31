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

// 打开文件，成功返回文件描述符，失败返回-1
int32_t file_open(uint32_t inode_no, uint8_t flag) {
    // 全局文件表找空位
    int fd_idx = get_free_slot_in_global();
    if (fd_idx == -1) {
        printk("exceed max open files\n");
        return -1;
    }
    // 填写空位
    file_table[fd_idx].fd_inode = inode_open(cur_part, inode_no);
    file_table[fd_idx].fd_pos = 0;
    file_table[fd_idx].fd_flag = flag;

    bool *write_deny = &file_table[fd_idx].fd_inode->write_deny;
    if (flag & O_WRONLY || flag & O_RDWR) {
        enum intr_status old_status = intr_disable();
        if (!(*write_deny)) {
            // 当前没有其他进程写该文件
            *write_deny = true;
            intr_set_status(old_status);
        } else {
            intr_set_status(old_status);
            printk("file can't be write now, try again later\n");
            memset(&file_table[fd_idx], 0, sizeof(struct file));
            return -1;
        }
    }
    return pcb_fd_install(fd_idx);
}

// 关闭文件
int32_t file_close(struct file *file) {
    if (file == NULL) {
        return -1;
    }
    if (file->fd_inode->write_deny) {
        file->fd_inode->write_deny = false;
    }
    inode_close(file->fd_inode);
    file->fd_inode = NULL;
    return 0;
}

// 把buf中count字节写入file，成功返回写入的字节数，失败返回-1
int32_t file_write(struct file *file, const void *buf, uint32_t count) {
    if ((file->fd_inode->i_size + count) > (BLOCK_SIZE * 140)) {
        printk("exceed max file_size 71680 bytes, write file failed\n");
        return -1;
    }
    uint8_t *io_buf = sys_malloc(BLOCK_SIZE);
    if (io_buf == NULL) {
        printk("file_write: sys_malloc for io_buf failed\n");
        return -1;
    }
    // 用来记录目标文件所有的块地址，准备好之后将根据此将数据一并写入到磁盘
    uint32_t *all_blocks = (uint32_t *)sys_malloc(BLOCK_SIZE + 48);
    if (all_blocks == NULL) {
        printk("file_write: sys_malloc for all_blocks failed\n");
        return -1;
    }

    const uint8_t *src = buf; // 待写入的数据
    uint32_t bytes_written = 0; // 已写入的数据大小
    uint32_t size_left = count; // 还未写入的数据大小
    uint32_t block_lba = -1;
    uint32_t block_bitmap_idx = 0;

    uint32_t sec_idx; // 写入数据时的扇区索引
    uint32_t sec_lba; // 写入数据时的扇区地址
    uint32_t sec_off_bytes; // 扇区内字节偏移量
    uint32_t sec_left_bytes; // 扇区内剩余字节量
    uint32_t chunk_size; // 单次写入硬盘的数据大小
    int32_t indirect_block_table; // 一级间接表的地址
    uint32_t block_idx;
    // 如果文件还没写入过数据，先分配一个块
    if (file->fd_inode->i_sectors[0] == 0) {
        block_lba = block_bitmap_alloc(cur_part);
        if (block_lba == -1) {
            printk("file_write: block_bitmap_alloc failed\n");
            return -1;
        }
        file->fd_inode->i_sectors[0] = block_lba;
        // 及时同步修改的位图到硬盘
        block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;
        ASSERT(block_bitmap_idx != 0);
        bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);
    }
    // 写入数据前，该文件已经占用的块数
    uint32_t file_has_used_blocks;
    if (file->fd_inode->i_size % BLOCK_SIZE == 0 && file->fd_inode->i_size > 0) {
        file_has_used_blocks = file->fd_inode->i_size / BLOCK_SIZE;
    } else {
        file_has_used_blocks = file->fd_inode->i_size / BLOCK_SIZE + 1;
    }
    // printk("file_has_used_blocks: %d\n", file_has_used_blocks);
    // 写入数据后，该文件将占用的块数
    uint32_t file_will_use_blocks;
    if ((file->fd_inode->i_size + count) % BLOCK_SIZE == 0 && (file->fd_inode->i_size + count) > 0) {
        file_will_use_blocks = (file->fd_inode->i_size + count) / BLOCK_SIZE;
    } else {
        file_will_use_blocks = (file->fd_inode->i_size + count) / BLOCK_SIZE + 1;
    }
    // printk("file_will_use_blocks: %d\n", file_will_use_blocks);
    ASSERT(file_will_use_blocks <= 140);
    // 写入前后的块数差值，判断是否要申请新扇区
    uint32_t add_blocks = file_will_use_blocks - file_has_used_blocks;
    // 准备all_blocks
    if (add_blocks == 0) {
        // 不需要分配新扇区
        if (file_will_use_blocks <= 12) {
            // 数据量在12块之内
            block_idx = file_has_used_blocks - 1;
            all_blocks[block_idx] = file->fd_inode->i_sectors[block_idx];
        } else {
            // 已经在间接块了，读入间接表的内容到all_blocks的12项开始的地方
            ASSERT(file->fd_inode->i_sectors[12] != 0);
            indirect_block_table = file->fd_inode->i_sectors[12];
            ide_read(cur_part->my_disk, indirect_block_table, all_blocks + 12, 1);
        }
    } else {
        // 有增量，需要分配新的扇区
        if (file_will_use_blocks <= 12) {
            // 写入之后的大小也在12个扇区之内
            block_idx = file_has_used_blocks - 1;
            ASSERT(file->fd_inode->i_sectors[block_idx] != 0);
            all_blocks[block_idx] = file->fd_inode->i_sectors[block_idx];
            block_idx = file_has_used_blocks;
            while (block_idx < file_will_use_blocks) {
                // 填充要新分配的扇区号
                block_lba = block_bitmap_alloc(cur_part);
                if (block_lba == -1) {
                    printk("file_write: block_bitmap_alloc for situation 1 failed\n");
                    return -1;
                }
                ASSERT(file->fd_inode->i_sectors[block_idx] == 0);
                file->fd_inode->i_sectors[block_idx] = all_blocks[block_idx] = block_lba;
                // 及时更新块位图到硬盘
                block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;
                bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);

                block_idx++;
            }
        } else if (file_has_used_blocks <= 12 && file_will_use_blocks > 12) {
            // 旧数据在12个直接块内，新数据要使用到间接块，意味着要分配间接表
            block_idx = file_has_used_blocks - 1;
            all_blocks[block_idx] = file->fd_inode->i_sectors[block_idx];

            // 创建一级间接表
            block_lba = block_bitmap_alloc(cur_part);
            if (block_lba == -1) {
                printk("file_write: block_bitmap_alloc for situation 2 failed\n");
                return -1;
            }
            ASSERT(file->fd_inode->i_sectors[12] == 0);
            indirect_block_table = file->fd_inode->i_sectors[12] = block_lba;
            // 填充要新分配的扇区号
            block_idx = file_has_used_blocks;
            while (block_idx < file_will_use_blocks) {
                block_lba = block_bitmap_alloc(cur_part);
                if (block_lba == -1) {
                    printk("file_write: block_bitmap_alloc for situation 2 failed\n");
                    return -1;
                }
                if (block_idx < 12) {
                    ASSERT(file->fd_inode->i_sectors[block_idx] == 0);
                    // 前12个直接块还要填充inode的sectors
                    file->fd_inode->i_sectors[block_idx] = all_blocks[block_idx] = block_lba;
                } else {
                    // 从第12块开始，先填充all_blcoks，之后再把all_blocks的12块之后的内容一起全部写入到间接表中
                    all_blocks[block_idx] = block_lba;
                }
                // 及时同步块位图到硬盘
                block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;
                bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);

                block_idx++;

            }
            // 将间接表的内容写入到硬盘
            ide_write(cur_part->my_disk, indirect_block_table, all_blocks + 12, 1);
        } else if (file_has_used_blocks > 12) {
            // 原来的大小就已经占用了间接块，说明间接表已经存在
            ASSERT(file->fd_inode->i_sectors[12] != 0);
            // 间接表地址
            indirect_block_table = file->fd_inode->i_sectors[12];
            // 从硬盘中读入间接表内容
            ide_read(cur_part->my_disk, indirect_block_table, all_blocks + 12, 1);
            
            // 填充新分配的扇区号
            block_idx = file_has_used_blocks;
            while (block_idx < file_will_use_blocks) {
                block_lba = block_bitmap_alloc(cur_part);
                if (block_lba == -1) {
                    printk("file_write: block_bitmap_alloc for situation 3 failed\n");
                    return -1;
                }
                all_blocks[block_idx++] = block_lba;
                // 及时同步块位图到硬盘
                block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;
                bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);
            }
            // 将改动后的间接表内容同步回硬盘
            ide_write(cur_part->my_disk, indirect_block_table, all_blocks + 12, 1); // 同步一级间接块表到硬盘
        }
    }
    // all_blocks已经准备完成，开始写入数据
    bool first_write_block = true; // 第一块的话可能是原有数据的最后一块，所以并不是整块写入，而是写入到原有数据的后面
    file->fd_pos = file->fd_inode->i_size - 1;
    while (bytes_written < count) {
        memset(io_buf, 0, BLOCK_SIZE);
        sec_idx = file->fd_inode->i_size / BLOCK_SIZE;
        sec_lba = all_blocks[sec_idx]; // 本次写入的扇区号
        sec_off_bytes = file->fd_inode->i_size % BLOCK_SIZE;
        sec_left_bytes = BLOCK_SIZE - sec_off_bytes;
        // 本次写入的数据块大小，打算一次最多写入一个块
        chunk_size = size_left < sec_left_bytes ? size_left : sec_left_bytes;
        if (first_write_block) {
            // 第一个块要从硬盘读到内存中，然后追加数据到原有数据后方，直到填满这个块，然后写回硬盘
            ide_read(cur_part->my_disk, sec_lba, io_buf, 1);
            first_write_block = false;
        }
        memcpy(io_buf + sec_off_bytes, src, chunk_size);
        ide_write(cur_part->my_disk, sec_lba, io_buf, 1);
        // printk("file write at lba 0x%x\n", sec_lba);
        src += chunk_size;
        // 更新文件大小
        file->fd_inode->i_size += chunk_size;
        file->fd_pos += chunk_size;
        bytes_written += chunk_size;
        size_left -= chunk_size;
    }
    // 数据写入完毕之后
    // 同步新的inode数据到硬盘
    inode_sync(cur_part, file->fd_inode, io_buf);
    sys_free(all_blocks);
    sys_free(io_buf);
    return bytes_written;
}

// 从file中读取count字节到buf，成功返回读出的字节数，失败返回-1
int32_t file_read(struct file *file, void *buf, uint32_t count) {
    uint8_t *buf_dst = (uint8_t *)buf;
    uint32_t size = count, size_left = size;
    // 若要读取的字节数超过了文件可读的数量，就读出剩余量
    if ((file->fd_pos + count) > file->fd_inode->i_size) {
        size = file->fd_inode->i_size - file->fd_pos;
        size_left = size;
        if (size_left == 0) {
            return -1;
        }
    }

    uint8_t *io_buf = sys_malloc(BLOCK_SIZE);
    if (io_buf == NULL) {
        printk("file_read: sys_malloc for io_buf failed\n");
    }
    uint32_t *all_blocks = (uint32_t *)sys_malloc(BLOCK_SIZE + 48);
    if (all_blocks == NULL) {
        printk("file_read: sys_malloc for all_blocks failed\n");
        return -1;
    }
    // 数据块起始索引
    uint32_t block_read_start_idx = file->fd_pos / BLOCK_SIZE;
    // 数据块终止索引
    uint32_t block_read_end_idx = (file->fd_pos + size) / BLOCK_SIZE;

    uint32_t read_blocks = block_read_end_idx - block_read_start_idx;
    ASSERT(block_read_start_idx < 139 && block_read_end_idx < 139);

    int32_t indirect_block_table; // 一级间接表地址
    uint32_t block_idx;
    // 开始填充all_blocks
    if (read_blocks == 0) {
        // 读数据不用跨扇区
        ASSERT(block_read_end_idx == block_read_start_idx);
        if (block_read_end_idx < 12) {
            // 直接块
            block_idx = block_read_end_idx;
            all_blocks[block_idx] = file->fd_inode->i_sectors[block_idx];
        } else {
            // 间接块
            indirect_block_table = file->fd_inode->i_sectors[12];
            ide_read(cur_part->my_disk, indirect_block_table, all_blocks + 12, 1);
        }
    } else {
        // 读数据要跨扇区
        if (block_read_end_idx < 12) {
            // 要读的数据都在直接块中
            block_idx = block_read_start_idx;
            while (block_idx <= block_read_end_idx) {
                all_blocks[block_idx] = file->fd_inode->i_sectors[block_idx];
                block_idx++;
            }
        } else if (block_read_start_idx < 12 && block_read_end_idx >= 12) {
            // 横跨直接块和间接块
            block_idx = block_read_start_idx;
            while (block_idx < 12) {
                all_blocks[block_idx] = file->fd_inode->i_sectors[block_idx];
                block_idx++;
            }
            ASSERT(file->fd_inode->i_sectors[12] != 0);
            indirect_block_table = file->fd_inode->i_sectors[12];
            ide_read(cur_part->my_disk, indirect_block_table, all_blocks + 12, 1);
        } else {
            // 数据全在间接块中
            ASSERT(file->fd_inode->i_sectors[12] != 0);
            indirect_block_table = file->fd_inode->i_sectors[12];
            ide_read(cur_part->my_disk, indirect_block_table, all_blocks + 12, 1);
        }
    }
    // 开始读取数据
    uint32_t sec_idx, sec_lba, sec_off_bytes, sec_left_bytes, chunk_size;
    uint32_t bytes_read = 0;
    while (bytes_read < size) {
        sec_idx = file->fd_pos / BLOCK_SIZE;
        sec_lba = all_blocks[sec_idx];
        sec_off_bytes = file->fd_pos % BLOCK_SIZE;
        sec_left_bytes = BLOCK_SIZE - sec_off_bytes;
        chunk_size = size_left < sec_left_bytes ? size_left : sec_left_bytes;
        memset(io_buf, 0, BLOCK_SIZE);
        ide_read(cur_part->my_disk, sec_lba, io_buf, 1);
        memcpy(buf_dst, io_buf + sec_off_bytes, chunk_size);
        buf_dst += chunk_size;
        file->fd_pos += chunk_size;
        bytes_read += chunk_size;
        size_left -= chunk_size;
    }
    sys_free(all_blocks);
    sys_free(io_buf);
    return bytes_read;
}