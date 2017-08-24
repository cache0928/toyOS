#ifndef __DEVICE_IDE_H
#define __DEVICE_IDE_H
#include "stdint.h"
#include "list.h"
#include "bitmap.h"
#include "sync.h"
// 分区
struct partition {
    uint32_t start_lba;
    uint32_t sec_cnt;
    struct disk *my_disk;
    struct list_elem part_tag;
    char name[8];
    // 用于文件系统
    void *sb;
    struct bitmap block_bitmap;
    struct bitmap inode_bitmap;
    struct list open_inodes;

};
// 硬盘
struct disk {
    char name[8];
    struct ide_channel *my_channel;
    uint8_t dev_no;
    // 最多4个主分区
    struct partition prim_parts[4];
    // 最多8个逻辑分区
    struct partition logic_parts[8];

};
// IDE通道
struct ide_channel {
    char name[8];
    uint16_t port_base; // 通道的起始端口号
    uint8_t irq_no; // 中断号，IDE0接在IRQ14，IDE1接在IRQ15
    struct lock lock;
    bool expecting_intr; // 表示等待硬盘中断
    struct semaphore disk_done;
    struct disk devices[2]; // 主从硬盘
};
void ide_init();
void ide_read(struct disk *hd, uint32_t lba, void *buf, uint32_t sec_cnt);
void ide_write(struct disk *hd, uint32_t lba, void *buf, uint32_t sec_cnt);
#endif