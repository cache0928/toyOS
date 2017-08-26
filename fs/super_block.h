#ifndef __FS_SUPER_BLOCK_H
#define __FS_SUPER_BLOCK_H
#include "stdint.h"

struct super_block {
    uint32_t magic; // 魔数，用来标示文件系统类型

    uint32_t sec_cnt; // 分区总扇区数
    uint32_t inode_cnt; // 分区中inode的数量
    uint32_t part_lba_base; // 分区的起始扇区

    uint32_t block_bitmap_lba; // 块位图的起始扇区
    uint32_t block_bitmap_sects; // 块位图所占用的扇区数量

    uint32_t inode_bitmap_lba; // inode位图的起始扇区
    uint32_t inode_bitmap_sects; //inode位图所占的扇区数

    uint32_t inode_table_lba; // inode表起始扇区
    uint32_t inode_table_sects; // inode表所占的扇区数

    uint32_t data_start_lba; // 数据区的起始扇区
    uint32_t root_inode_no; // 根目录所在的inode号
    uint32_t dir_entry_size; // 目录项大小

    uint8_t pad[460]; // 填充占位，充满一个扇区512字节
} __attribute__((packed));


#endif