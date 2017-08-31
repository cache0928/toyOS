#ifndef __FS_INODE_H
#define __FS_INODE_H

#include "stdint.h"
#include "global.h"
#include "list.h"
#include "ide.h"
/*
本inode只支持一级间接索引块
块大小设置为一个扇区大小，即512字节
因为一个块地址是32位，所以间接索引块一共可以表示 512 ／ 4 = 128 数据块
所以一个文件最多只能占用12+128 = 140块
*/

struct inode {
    uint32_t i_no; // inode编号
    uint32_t i_size; // 文件大小，若是目录则表示目录下的所有目录项大小
    uint32_t i_open_cnts; // 文件被打开的次数
    bool write_deny; // 文件是否正在被写入
    uint32_t i_sectors[13]; // 0-11是直接块，12是一级间接索引块
    struct list_elem inode_tag;
};

struct inode *inode_open(struct partition *part, uint32_t inode_no);
void inode_sync(struct partition *part, struct inode *inode, void *io_buf);
void inode_init(uint32_t inode_no, struct inode *new_inode);
void inode_close(struct inode *inode);
void inode_release(struct partition *part, uint32_t inode_no);
#endif