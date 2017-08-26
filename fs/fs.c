#include "fs.h"
#include "ide.h"
#include "global.h"
#include "stdio-kernel.h"
#include "string.h"
#include "bitmap.h"
#include "debug.h"
#include "memory.h"
#include "dir.h"
#include "inode.h"
#include "super_block.h"
#include "list.h"


// 格式化，创建文件系统
static void partition_format(struct partition *part) {
    // 引导区所占用的扇区数
    uint32_t boot_sector_sects = 1;
    // 超级块所占用的扇区数
    uint32_t super_block_sects = 1;
    // inode位图所占用扇区数
    uint32_t inode_bitmap_sects = DIV_ROUND_UP(MAX_FILES_PER_PART, BITS_PER_SECTOR);
    // inode表占用的扇区数
    uint32_t inode_table_sects = DIV_ROUND_UP((sizeof(struct inode) * MAX_FILES_PER_PART), SECTOR_SIZE);
    // 引导块+超级块+inode位图+inode数组占用的扇区数
    uint32_t used_sects = boot_sector_sects + super_block_sects + inode_bitmap_sects + inode_table_sects;
    // 用于空闲块位图和空闲块的扇区数
    uint32_t free_sects = part->sec_cnt - used_sects;

    // 粗糙处理空闲块的扇区数
    uint32_t block_bitmap_sects;
    block_bitmap_sects = DIV_ROUND_UP(free_sects, BITS_PER_SECTOR);
    // 空闲块的数量
    uint32_t free_block_sects = free_sects - block_bitmap_sects;
    // 根据空闲块数量重新算空闲块位图的扇区数
    block_bitmap_sects = DIV_ROUND_UP(free_block_sects, BITS_PER_SECTOR);

    // 初始化超级块
    struct super_block sb;
    sb.magic = 0x19920324;
    sb.sec_cnt = part->sec_cnt;
    sb.inode_cnt = MAX_FILES_PER_PART;
    sb.part_lba_base = part->start_lba;

    sb.block_bitmap_lba = sb.part_lba_base + 2;
    sb.block_bitmap_sects = block_bitmap_sects;

    sb.inode_bitmap_lba = sb.block_bitmap_lba + sb.block_bitmap_sects;
    sb.inode_bitmap_sects = inode_bitmap_sects;

    sb.inode_table_lba = sb.inode_bitmap_lba + sb.inode_bitmap_sects;
    sb.inode_table_sects = inode_table_sects;

    sb.data_start_lba = sb.inode_table_lba + sb.inode_table_sects;
    
    sb.root_inode_no = 0;
    sb.dir_entry_size = sizeof(struct dir_entry);

    printk("%s info:\n", part->name);
    printk("   magic:0x%x\n   part_lba_base:0x%x\n   all_sectors:0x%x\n   inode_cnt:0x%x\n   block_bitmap_lba:0x%x\n   block_bitmap_sectors:0x%x\n   inode_bitmap_lba:0x%x\n   inode_bitmap_sectors:0x%x\n   inode_table_lba:0x%x\n   inode_table_sectors:0x%x\n   data_start_lba:0x%x\n", sb.magic, sb.part_lba_base, sb.sec_cnt, sb.inode_cnt, sb.block_bitmap_lba, sb.block_bitmap_sects, sb.inode_bitmap_lba, sb.inode_bitmap_sects, sb.inode_table_lba, sb.inode_table_sects, sb.data_start_lba);

    struct disk *hd = part->my_disk;
    // 写入超级块
    ide_write(hd, part->start_lba+1, &sb, 1);
    printk("   super_block_lba:0x%x\n", part->start_lba + 1);
    
    // 堆里申请一空间，将用来初始化inode_bitmap, inode_table, block_bitmap 
    uint32_t buf_size = (sb.block_bitmap_sects >= sb.inode_bitmap_sects ? sb.block_bitmap_sects : sb.inode_bitmap_sects);
    buf_size = (buf_size >= sb.inode_table_sects ? buf_size : sb.inode_table_sects) * SECTOR_SIZE;
    uint8_t* buf = (uint8_t *)sys_malloc(buf_size);

    // 初始block_bitmap
    buf[0] |= 0x01; // 第0块分配给根目录
    // 空闲块位图的最后一字节
    uint32_t block_bitmap_last_byte = free_block_sects / 8;
    // 空闲块位图最后一字节中应该包括在位图内的位
    uint8_t block_bitmap_last_bit = free_block_sects % 8;
    // 空闲块位图所在扇区的最后一个扇区中，不属于空闲块位图的部分，应全部置1表示不可用
    uint32_t last_size = SECTOR_SIZE - (block_bitmap_last_byte % SECTOR_SIZE);
    memset(&buf[block_bitmap_last_byte], 0xff, last_size);
    uint8_t bit_idx = 0;
    while (bit_idx <= block_bitmap_last_bit) {
        buf[block_bitmap_last_byte] &= ~(1 << bit_idx++);
    }
    // 写入block_bitmap
    ide_write(hd, sb.block_bitmap_lba, buf, sb.block_bitmap_sects);

    // 初始化inode_bitmap
    memset(buf, 0, buf_size);
    buf[0] |= 0x01; // 第0个inode指向根目录
    ide_write(hd, sb.inode_bitmap_lba, buf, sb.inode_bitmap_sects);

    // 初始化inode_table
    memset(buf, 0, buf_size);
    // 填写第0个inode，即根目录
    struct inode *i = (struct inode *)buf;
    i->i_size = sb.dir_entry_size * 2; // .和..
    i->i_no = 0;
    i->i_sectors[0] = sb.data_start_lba; // 指向第0块
    ide_write(hd, sb.inode_table_lba, buf, sb.inode_table_sects);

    // 写入根目录到空闲块第0块，其中包括两个目录项.和..
    memset(buf, 0, buf_size);
    struct dir_entry *p_de = (struct dir_entry *)buf;
    memcpy(p_de->filename, ".", 1);
    p_de->i_no = 0;
    p_de->f_type = FT_DIRECTORY;
    p_de++;
    // 根目录的父目录是其本身
    memcpy(p_de->filename, "..", 1);
    p_de->i_no = 0;
    p_de->f_type = FT_DIRECTORY;
    ide_write(hd, sb.data_start_lba, buf, 1);

    printk("   root_dir_lba:0x%x\n", sb.data_start_lba);
    printk("%s format done\n", part->name);
    sys_free(buf);
}

struct partition *cur_part; // 当前挂载的分区
// 挂载指定arg（对应char *，分区名）对应的分区， 用在分区队列 partition_list的遍历时
static bool mount_partition(struct list_elem *part_elem, int arg) {
    char *part_name = (char *)arg;
    struct partition *part = elem2entry(struct partition, part_tag, part_elem);
    if (!strcmp(part->name, part_name)) {
        // 如果名字相同，则表示是指定的分区，进行挂载，即拷贝文件系统的元信息到内存中
        cur_part = part;
        struct disk *hd = cur_part->my_disk;
        // 拷贝超级块
        struct super_block *sb_buf = (struct super_block *)sys_malloc(SECTOR_SIZE);
        cur_part->sb = (struct super_block *)sys_malloc(sizeof(struct super_block));
        // printk("size of super block: %d\n", sizeof(struct super_block));
        if (cur_part->sb == NULL) {
            PANIC("alloc memory failed");
        }
        memset(sb_buf, 0, SECTOR_SIZE);
        ide_read(hd, cur_part->start_lba + 1, sb_buf, 1);
        memcpy(cur_part->sb, sb_buf, sizeof(struct super_block));
        // 拷贝空闲块位图
        cur_part->block_bitmap.bits = (uint8_t *)sys_malloc(sb_buf->block_bitmap_sects * SECTOR_SIZE);
        if (cur_part->block_bitmap.bits == NULL) {
            PANIC("alloc memory failed");
        }
        cur_part->block_bitmap.btmp_bytes_len = sb_buf->block_bitmap_sects * SECTOR_SIZE;
        ide_read(hd, sb_buf->block_bitmap_lba, cur_part->block_bitmap.bits, sb_buf->block_bitmap_sects);
        // 拷贝inode位图
        cur_part->inode_bitmap.bits = (uint8_t *)sys_malloc(sb_buf->inode_bitmap_sects * SECTOR_SIZE);
        if (cur_part->inode_bitmap.bits == NULL) {
            PANIC("alloc memory failed");
        }
        cur_part->inode_bitmap.btmp_bytes_len = sb_buf->inode_bitmap_sects * SECTOR_SIZE;
        ide_read(hd, sb_buf->inode_bitmap_lba, cur_part->inode_bitmap.bits, sb_buf->inode_bitmap_sects);

        list_init(&cur_part->open_inodes);
        printk("mount %s done!\n", cur_part->name);
        // 停止遍历
        return true;
    }
    // 继续遍历
    return false;
}

void filesys_init() {
    uint8_t channel_no = 0, dev_no = 0, part_idx = 0;
    // 用来存放读入的超级块
    struct super_block *sb_buf = (struct super_block *)sys_malloc(SECTOR_SIZE);
    if (sb_buf == NULL) {
        PANIC("alloc memory failed");
    }
    printk("searching filesystem...\n");
    while (channel_no < channel_cnt) {
        // 遍历通道
        dev_no = 0;
        while (dev_no < 2) {
            // 遍历主从硬盘
            if (dev_no == 0) {
                dev_no++; 
                continue; // 跨过系统盘
            }
            struct disk *hd = &channels[channel_no].devices[dev_no];
            struct partition *part = hd->prim_parts;
            while(part_idx < 12) {
                // 遍历分区，最多支持4个主分区和8个逻辑分区
                if (part_idx == 4) {
                    // 第4个开始就指向逻辑分区了
                    part = hd->logic_parts;
                }
                if (part->sec_cnt != 0) {
                    // 分区有效, 读入超级块
                    memset(sb_buf, 0, SECTOR_SIZE);
                    ide_read(hd, part->start_lba+1, sb_buf, 1);
                    if (sb_buf->magic == 0x19920324) {
                        // 只支持自己的文件系统
                        printk("%s has filesystem\n", part->name);
                    } else {
                        // 文件系统不存在，格式化
                        printk("formatting %s's partition %s...\n", hd->name, part->name);
                        partition_format(part);
                    }
                }
                part_idx++;
                part++;
            }
            dev_no++;
        }
        channel_no++;
    }
    sys_free(sb_buf);
    char default_part[8] = "sdb1";
    list_traversal(&partition_list, mount_partition, (int)default_part);
}