#include "ide.h"
#include "stdio-kernel.h"
#include "stdio.h"
#include "sync.h"
#include "debug.h"
#include "timer.h"
#include "io.h"
#include "interrupt.h"
#include "string.h"
// 硬盘寄存器的端口
#define reg_data(channel) (channel->port_base + 0) 
#define reg_error(channel) (channel->port_base + 1) 
#define reg_sect_cnt(channel) (channel->port_base + 2) 
#define reg_lba_l(channel) (channel->port_base + 3) 
#define reg_lba_m(channel) (channel->port_base + 4) 
#define reg_lba_h(channel) (channel->port_base + 5) 
#define reg_dev(channel) (channel->port_base + 6) 
#define reg_status(channel) (channel->port_base + 7) 
#define reg_cmd(channel) (reg_status(channel)) 
#define reg_alt_status(channel) (channel->port_base + 0x206) 
#define reg_ctl(channel) (reg_alt_status(channel))
// status寄存器的关键位
#define BIT_STAT_BSY 0x80 // 硬盘忙  
#define BIT_STAT_DRDY 0x40 // 驱动器准备好 
#define BIT_STAT_DRQ 0x8 // 数据传输准备好了
// device寄存器的关键位
#define BIT_DEV_MBS 0xa0 // 第7和第5位固定为1
#define BIT_DEV_LBA 0x40 
#define BIT_DEV_DEV 0x10
// 硬盘操作指令
#define CMD_IDENTIFY 0xec
#define CMD_READ_SECTOR 0x20 
#define CMD_WRITE_SECTOR 0x30

#define max_lba ((80*1024*1024/512) - 1)

uint8_t channel_cnt; // 通道数
struct ide_channel channels[2]; // 最多2通道

// 选择需要读写操作的目标硬盘
static void select_disk(struct disk *hd) {
    uint8_t reg_device = BIT_DEV_MBS | BIT_DEV_LBA;
    if (hd->dev_no == 1) {
        // 是从盘的话dev位也要置1
        reg_device |= BIT_DEV_DEV;
    }
    outb(reg_dev(hd->my_channel), reg_device);
}

// 选取要读写的起始扇区和读写的总扇区数
static void select_sector(struct disk *hd, uint32_t lba, uint8_t sec_cnt) {
    ASSERT(lba <= max_lba);
    struct ide_channel *channel = hd->my_channel;
    // 写入扇区数, 如果是0，则表示256个扇区
    outb(reg_sect_cnt(channel), sec_cnt);
    // 写入起始lba扇区地址，最高的4位在device寄存器的低4位
    outb(reg_lba_l(channel), lba);
    outb(reg_lba_m(channel), lba >> 8);
    outb(reg_lba_h(channel), lba >> 16);
    outb(reg_dev(channel), BIT_DEV_MBS | BIT_DEV_LBA | (hd->dev_no == 1 ? BIT_DEV_DEV : 0) | lba >> 24);
}

// 向硬盘控制器发命令
static void cmd_out(struct ide_channel *channel, uint8_t cmd) {
    // 发了命令就表示该通道正在等待硬盘完成的时候触发中断
    channel->expecting_intr = true;
    outb(reg_cmd(channel), cmd);
}

// 从硬盘读入sec_cnt个扇区的数据到buf
// 要在设置完起始扇区和需要读入的扇区数，已经向硬盘发送读指令，且硬盘已经准备好之后调用
static void read_from_sector(struct disk *hd, void *buf, uint8_t sec_cnt) {
    uint32_t size_in_byte;
    if (sec_cnt == 0) {
        size_in_byte = 256 * 512;
    } else {
        size_in_byte = sec_cnt * 512;
    }
    // data端口是16位的
    insw(reg_data(hd->my_channel), buf, size_in_byte / 2);
}

// 将sec_cnt个扇区的数据写入到硬盘
// 要在设置完起始扇区和需要写入的扇区数后调用，已经向硬盘发送写指令，且硬盘已经准备好之后调用
static void write2sector(struct disk *hd, void *buf, uint8_t sec_cnt) {
    uint32_t size_in_byte;
    if (sec_cnt == 0) {
        size_in_byte = 256 * 512;
    } else {
        size_in_byte = sec_cnt * 512;
    }
    // data端口是16位的
    outsw(reg_data(hd->my_channel), buf, size_in_byte / 2);
}

// 等待硬盘操作
static bool busy_wait(struct disk *hd) {
    struct ide_channel *channel = hd->my_channel;
    uint16_t time_limit = 30 * 1000;
    while (time_limit -= 10 >= 0) {
        if (!(inb(reg_status(channel)) & BIT_STAT_BSY)) {
            // DRQ为1说明硬盘已经准备好
            return (inb(reg_status(channel)) & BIT_STAT_DRQ);
        } else {
            mtime_sleep(10);
        }
    }
    // 超时代表硬盘出错
    return false;
}

// 从硬盘读取lba起始的sec_cnt个扇区数据到buf
void ide_read(struct disk *hd, uint32_t lba, void *buf, uint32_t sec_cnt) {
    ASSERT(lba <= max_lba);
    ASSERT(sec_cnt > 0);
    // 通道操作要保证原子性，否则无法判断硬盘中断是否是此次操作引起的
    lock_acquire(&hd->my_channel->lock);
    // 选择操作的硬盘
    select_disk(hd);
    // 因为Sector Count寄存器8位，所以一次最多读256个扇区，超过256个扇区要分批
    uint32_t secs_op; // 单批读多少扇区
    uint32_t secs_done = 0; // 已经读了多少扇区
    while (secs_done < sec_cnt) {
        if (secs_done + 256 <= sec_cnt) {
            secs_op = 256;
        } else {
            secs_op = sec_cnt - secs_done;
        }
        // 写入要读取的起始扇区和扇区数
        select_sector(hd, lba + secs_done, secs_op);
        // 发送读取指令
        cmd_out(hd->my_channel, CMD_READ_SECTOR);
        // 因为硬盘处理要好一会，所以此处阻塞自己，等待硬盘操作完成中断唤醒
        sema_down(&hd->my_channel->disk_done);
        // 醒来之后先检测硬盘是否准备好了
        if (!busy_wait(hd)) {
            char error[64];
            sprintf(error, "%s read sector %d failed!!!!!!\n", hd->name, lba);
            PANIC(error);
        }
        // 将数据读取到指定位置
        read_from_sector(hd, (void *)((uint32_t)buf + secs_done * 512), secs_op);
        secs_done += secs_op;
    }
    lock_release(&hd->my_channel->lock);
}

// 将数据写入到硬盘lba起始的sec_cnt个扇区内
void ide_write(struct disk *hd, uint32_t lba, void *buf, uint32_t sec_cnt) {
    ASSERT(lba <= max_lba);
    ASSERT(sec_cnt > 0);
    // 通道操作要保证原子性，否则无法判断硬盘中断是否是此次操作引起的
    lock_acquire(&hd->my_channel->lock);
    // 选择操作的硬盘
    select_disk(hd);
    // 因为Sector Count寄存器8位，所以一次最多写入256个扇区，超过256个扇区要分批
    uint32_t secs_op; // 单批读多少扇区
    uint32_t secs_done = 0; // 已经读了多少扇区
    while (secs_done < sec_cnt) {
        if (secs_done + 256 <= sec_cnt) {
            secs_op = 256;
        } else {
            secs_op = sec_cnt - secs_done;
        }
        // 写入要写入的起始扇区和扇区数
        select_sector(hd, lba + secs_done, secs_op);
        // 发送写入指令
        cmd_out(hd->my_channel, CMD_WRITE_SECTOR);
        // 检测硬盘是否准备好了
        if (!busy_wait(hd)) {
            char error[64];
            sprintf(error, "%s write sector %d failed!!!!!!\n", hd->name, lba);
            PANIC(error);
        }
        // 写入数据
        write2sector(hd, (void*)((uint32_t)buf + secs_done * 512), secs_op);
        // 因为硬盘很慢，数据传输的过程中可以阻塞自己，等待硬盘完成后唤醒
        sema_down(&hd->my_channel->disk_done);
        secs_done += secs_op;
    }
    lock_release(&hd->my_channel->lock);
}

// 硬盘中断处理程序
void intr_hd_handler(uint8_t irq_no) {
    ASSERT(irq_no == 0x2e || irq_no == 0x2f);
    // 产生中断的通道号
    uint8_t ch_no = irq_no - 0x2e;
    struct ide_channel *channel = &channels[ch_no];
    ASSERT(channel->irq_no == irq_no);
    // 因为操作通道加锁的缘故
    // 如果这个通道的expecting_intr是1，则表示就是这个通道在等待中断
    if (channel->expecting_intr) {
        channel->expecting_intr = false;
        // 增加信号量来唤醒对应的线程
        sema_up(&channel->disk_done);
        // 读取一次status寄存器会让硬盘控制器认为此次中断已经被处理
        // 否则不能继续新的的读写
        inb(reg_status(channel));
    }
}


int32_t ext_lba_base = 0; // 总拓展分区的起始LBA
uint8_t p_no = 0, l_no = 0;	 // 用来记录硬盘主分区和逻辑分区的下标
struct list partition_list;	 // 分区队列

// 构建一个16字节大小的结构体,用来存分区表项
struct partition_table_entry {
    uint8_t  bootable;		 // 是否可引导	
    uint8_t  start_head;	 // 起始磁头号
    uint8_t  start_sec;		 // 起始扇区号
    uint8_t  start_chs;		 // 起始柱面号
    uint8_t  fs_type;		 // 分区类型
    uint8_t  end_head;		 // 结束磁头号
    uint8_t  end_sec;		 // 结束扇区号
    uint8_t  end_chs;		 // 结束柱面号
    uint32_t start_lba;		 // 本分区起始扇区的lba地址
    uint32_t sec_cnt;		 // 本分区的扇区数目
} __attribute__ ((packed));	 // 保证此结构是16字节大小
 
// 引导扇区, mbr或ebr的结构
struct boot_sector {
    uint8_t  other[446];		 // 引导代码
    struct   partition_table_entry partition_table[4];       // 分区表中有4项,共64字节
    uint16_t signature;		 // 启动扇区的结束标志是0x55,0xaa
} __attribute__ ((packed));

// 将dst中len个相邻字节交换位置后存入buf
static void swap_pairs_bytes(const char *dst, char *buf, uint32_t len) {
    uint8_t idx;
    for (idx = 0; idx < len; idx += 2) { 
        buf[idx+1] = *dst++;   
        buf[idx] = *dst++;   
    }
    buf[idx] = '\0';
}

// 获得硬盘参数信息
static void identify_disk(struct disk *hd) {
    char id_info[512];
    select_disk(hd);
    cmd_out(hd->my_channel, CMD_IDENTIFY);
    sema_down(&hd->my_channel->disk_done);
    if (!busy_wait(hd)) {
        char error[64];
        sprintf(error, "%s identify failed!!!!!!\n", hd->name);
        PANIC(error);
    }
    read_from_sector(hd, id_info, 1);
    char buf[64];
    uint8_t sn_start = 10 * 2, sn_len = 20, md_start = 27 * 2, md_len = 40;
    swap_pairs_bytes(&id_info[sn_start], buf, sn_len);
    printk("   disk %s info:\n      SN: %s\n", hd->name, buf);
    memset(buf, 0, sizeof(buf));
    swap_pairs_bytes(&id_info[md_start], buf, md_len);
    printk("      MODULE: %s\n", buf);
    uint32_t sectors = *(uint32_t *)&id_info[60 * 2];
    printk("      SECTORS: %d\n", sectors);
    printk("      CAPACITY: %dMB\n", sectors * 512 / 1024 / 1024);
}

// 扫描硬盘hd中地址为ext_lba的扇区中的所有分区
static void partition_scan(struct disk *hd, uint32_t ext_lba) {
    struct boot_sector *bs = sys_malloc(sizeof(struct boot_sector));
    ide_read(hd, ext_lba, bs, 1);
    uint8_t part_idx = 0;
    struct partition_table_entry *p = bs->partition_table;

    // 遍历4个分区表项
    while (part_idx++ < 4) {
        if (p->fs_type == 0x5) {	 // 若为扩展分区
            if (ext_lba_base != 0) { 
                // 子拓展分区
                partition_scan(hd, p->start_lba + ext_lba_base);
            } else {
                // 总拓展分区
                ext_lba_base = p->start_lba;
                partition_scan(hd, p->start_lba);
            }
        } else if (p->fs_type != 0) { // 若是有效的分区类型
            if (ext_lba == 0) {	 
                // 主分区
                hd->prim_parts[p_no].start_lba = ext_lba + p->start_lba;
                hd->prim_parts[p_no].sec_cnt = p->sec_cnt;
                hd->prim_parts[p_no].my_disk = hd;
                list_append(&partition_list, &hd->prim_parts[p_no].part_tag);
                sprintf(hd->prim_parts[p_no].name, "%s%d", hd->name, p_no + 1);
                p_no++;
                ASSERT(p_no < 4);
            } else {
                // 逻辑分区
                hd->logic_parts[l_no].start_lba = ext_lba + p->start_lba;
                hd->logic_parts[l_no].sec_cnt = p->sec_cnt;
                hd->logic_parts[l_no].my_disk = hd;
                list_append(&partition_list, &hd->logic_parts[l_no].part_tag);
                sprintf(hd->logic_parts[l_no].name, "%s%d", hd->name, l_no + 5);	 // 逻辑分区数字是从5开始,主分区是1～4.
                l_no++;
                if (l_no >= 8)    // 只支持8个逻辑分区,避免数组越界
                return;
            }
        } 
        p++;
    }
    sys_free(bs);
}

// 打印分区信息
static bool partition_info(struct list_elem *pelem, int arg) {
    struct partition *part = elem2entry(struct partition, part_tag, pelem);
    printk("   %s start_lba:0x%x, sec_cnt:0x%x\n",part->name, part->start_lba, part->sec_cnt);
    // 纯粹为了list_traversal能遍历
    return false;
}

// 硬盘驱动初始化
void ide_init() {
    printk("ide_init start\n");
    // 硬盘数由BIOS存在0x475
    uint8_t hd_cnt = *((uint8_t *)(0x475));
    ASSERT(hd_cnt > 0);
    channel_cnt = DIV_ROUND_UP(hd_cnt, 2);
    list_init(&partition_list);
    struct ide_channel *channel;
    uint8_t channel_no = 0, dev_no = 0;
    while (channel_no < channel_cnt) {
        channel = &channels[channel_no];
        sprintf(channel->name, "ide%d", channel_no);
        switch (channel_no) {
            case 0:
                channel->port_base = 0x1f0;
                channel->irq_no = 0x20 + 14;
                break;
            case 1:
                channel->port_base = 0x170;
                channel->irq_no = 0x20 + 15;
                break;
        }
        channel->expecting_intr = false;
        lock_init(&channel->lock);
        sema_init(&channel->disk_done, 0);
        // 注册通道的中断处理函数
        register_handler(channel->irq_no, intr_hd_handler);
        // 分别获取两个硬盘的参数及分区信息
        while (dev_no < 2) {
            struct disk *hd = &channel->devices[dev_no];
            hd->my_channel = channel;
            hd->dev_no = dev_no;
            sprintf(hd->name, "sd%c", 'a' + channel_no * 2 + dev_no);
            identify_disk(hd);	 // 获取硬盘参数
            if (dev_no != 0) {	 // 内核本身的裸硬盘(hd60M.img)不处理
                partition_scan(hd, 0);  // 扫描该硬盘上的分区  
            }
            p_no = 0, l_no = 0;
            dev_no++; 
        }
        dev_no = 0;			  	   // 将硬盘驱动器号置0,为下一个channel的两个硬盘初始化。
        channel_no++;
    }
    printk("\n   all partition info\n");    
    list_traversal(&partition_list, partition_info, (int)NULL);
    printk("ide_init done\n");
}