#include "dir.h"
#include "stdint.h"
#include "inode.h"
#include "file.h"
#include "fs.h"
#include "stdio-kernel.h"
#include "global.h"
#include "debug.h"
#include "memory.h"
#include "string.h"
#include "interrupt.h"
#include "super_block.h"

struct dir root_dir; // 根目录

// 打开根目录
void open_root_dir(struct partition *part) {
    root_dir.inode = inode_open(part, part->sb->root_inode_no);
    root_dir.dir_pos = 0;
}

// 打开inode_no对应的目录，并返回目录指针
// 即加载目录对应inode到内存中，新建一个dir结构关联这个inode
struct dir *dir_open(struct partition *part, uint32_t inode_no) {
    struct dir *pdir = (struct dir *)sys_malloc(sizeof(struct dir));
    pdir->inode = inode_open(part, inode_no);
    pdir->dir_pos = 0;
    return pdir;
}

// 在part分区内的pdir目录中查找文件名为name的文件，找到返回true并将目录项写入dir_e
bool search_dir_entry(struct partition *part, struct dir *pdir, const char *name, struct dir_entry *dir_e) {
    uint32_t block_cnt = 140;	 // 12个直接块+128个一级间接块=140块

    /* 12个直接块大小+128个间接块,共560字节 */
    uint32_t *all_blocks = (uint32_t *)sys_malloc(48 + 512);
    if (all_blocks == NULL) {
        printk("search_dir_entry: sys_malloc for all_blocks failed");
        return false;
    }
    // 加载pdir目录的inode所有的sectors到all_block
    uint32_t block_idx = 0;
    while (block_idx < 12) {
        all_blocks[block_idx] = pdir->inode->i_sectors[block_idx];
        block_idx++;
    }
    block_idx = 0;
    if (pdir->inode->i_sectors[12] != 0) {
        // 若含有一级间接块表, 读入1级间接表的所有数据到all_blocks
        ide_read(part->my_disk, pdir->inode->i_sectors[12], all_blocks + 12, 1);
    }

    uint8_t *buf = (uint8_t *)sys_malloc(SECTOR_SIZE);
    struct dir_entry *p_de = (struct dir_entry *)buf;
    uint32_t dir_entry_size = part->sb->dir_entry_size;
    uint32_t dir_entry_cnt = SECTOR_SIZE / dir_entry_size;   // 1扇区内可容纳的目录项个数

    // 遍历pdir目录的inode对应的所有的块
    while (block_idx < block_cnt) {		  
        if (all_blocks[block_idx] == 0) {
            // 目录项不一定连续，因为文件可能增加或者删除，就会留下目录项空洞，所以就可能造成块也不连续
            block_idx++;
            continue;
        }
        // 读入一个块，然后遍历块中所有的目录项
        ide_read(part->my_disk, all_blocks[block_idx], buf, 1);
        uint32_t dir_entry_idx = 0;
        while (dir_entry_idx < dir_entry_cnt) {
            if (!strcmp(p_de->filename, name)) {
                memcpy(dir_e, p_de, dir_entry_size);
                sys_free(buf);
                sys_free(all_blocks);
                return true;
            }
            dir_entry_idx++;
            p_de++;
        }
        block_idx++;
        p_de = (struct dir_entry *)buf;
        memset(buf, 0, SECTOR_SIZE);
    }
    sys_free(buf);
    sys_free(all_blocks);
    return false;
}

// 关闭目录
void dir_close(struct dir *dir) {
    if (dir == &root_dir) {
        // 根目录的空间并不是malloc出来的，所以也不释放
        return;
    }
    inode_close(dir->inode);
    sys_free(dir);
}

// 根据提供的name、inode编号、file_type初始化一个目录项
void create_dir_entry(char *filename, uint32_t inode_no, uint8_t file_type, struct dir_entry *p_de) {
    ASSERT(strlen(filename) <=  MAX_FILE_NAME_LEN);
    memcpy(p_de->filename, filename, strlen(filename));
    p_de->i_no = inode_no;
    p_de->f_type = file_type;
}

// 将目录项写入到目录中
bool sync_dir_entry(struct dir *parent_dir, struct dir_entry *p_de, void *io_buf) {
    struct inode* dir_inode = parent_dir->inode;
    uint32_t dir_size = dir_inode->i_size;
    uint32_t dir_entry_size = cur_part->sb->dir_entry_size;
 
    ASSERT(dir_size % dir_entry_size == 0);	// dir_size应该是dir_entry_size的整数倍
 
    uint32_t dir_entrys_per_sec = (SECTOR_SIZE / dir_entry_size); // 每扇区最大的目录项数目
    int32_t block_lba = -1;
 
    // 加载parent_dir目录的inode所有的sectors到all_block
    uint8_t block_idx = 0;
    uint32_t all_blocks[140] = {0};
    while (block_idx < 12) {
        // 前12个是直接块，直接赋值
        all_blocks[block_idx] = dir_inode->i_sectors[block_idx];
        block_idx++;
    }
    block_idx = 0;
    if (dir_inode->i_sectors[12] != 0) {
        // 若含有一级间接块表, 读入1级间接表的所有数据到all_blocks
        ide_read(cur_part->my_disk, dir_inode->i_sectors[12], all_blocks + 12, 1);
    }
 
    struct dir_entry *dir_e = (struct dir_entry *)io_buf;	       // dir_e用来在io_buf中遍历目录项
    int32_t block_bitmap_idx = -1;
    // 开始遍历所有块以寻找目录项空位,若已有扇区中没有空闲位,
    // 在不超过文件大小的情况下申请新扇区来存储新目录项
    while (block_idx < 140) {
        block_bitmap_idx = -1;
        if (all_blocks[block_idx] == 0) {
            // 发现空闲的数据块，申请一个新的数据块，填入p_de目录项
            block_lba = block_bitmap_alloc(cur_part);
            if (block_lba == -1) {
                printk("alloc block bitmap for sync_dir_entry failed\n");
                return false;
            }
            // 及时同步改动过的位图到硬盘
            block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;
            ASSERT(block_bitmap_idx != -1);
            bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);

            block_bitmap_idx = -1;
            if (block_idx < 12) {	    
                // 直接块
                dir_inode->i_sectors[block_idx] = all_blocks[block_idx] = block_lba;
            } else if (block_idx == 12) {	  
                // 正好是一级间接表的位置，能到这里说明一级间接表是0，还没有配置
                // 所以要先申请一级间接表的空间，将lba填入此处，然后再申请一个扇区，存放目录项数据块，再将lba填入间接表
                dir_inode->i_sectors[12] = block_lba; // 间接表
                block_lba = -1;
                // 再分配一个块做为第0个间接块
                block_lba = block_bitmap_alloc(cur_part);
                if (block_lba == -1) {
                    block_bitmap_idx = dir_inode->i_sectors[12] - cur_part->sb->data_start_lba;
                    bitmap_set(&cur_part->block_bitmap, block_bitmap_idx, 0);
                    dir_inode->i_sectors[12] = 0;
                    printk("alloc block bitmap for sync_dir_entry failed\n");
                    return false;
                }

                // 及时同步位图的修改
                block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;
                ASSERT(block_bitmap_idx != -1);
                bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);

                all_blocks[12] = block_lba;
                // 把新的一级间接块表同步到硬盘
                ide_write(cur_part->my_disk, dir_inode->i_sectors[12], all_blocks + 12, 1);
            } else {	   
                // 已有间接块表而间接块未分配
                all_blocks[block_idx] = block_lba;
                // 把修改后的间接块表同步回硬盘
                ide_write(cur_part->my_disk, dir_inode->i_sectors[12], all_blocks + 12, 1);
            }

            // 将目录项p_de写入新建的数据块扇区
            memset(io_buf, 0, 512);
            memcpy(io_buf, p_de, dir_entry_size);
            ide_write(cur_part->my_disk, all_blocks[block_idx], io_buf, 1);
            dir_inode->i_size += dir_entry_size;
            return true;
        }

        // 若第block_idx块已存在,将其读进内存,然后在该块中查找空目录项
        ide_read(cur_part->my_disk, all_blocks[block_idx], io_buf, 1); 
        // 在对应的数据块中遍历目录项，寻找空闲目录项
        uint8_t dir_entry_idx = 0;
        while (dir_entry_idx < dir_entrys_per_sec) {
            if ((dir_e + dir_entry_idx)->f_type == FT_UNKNOWN) {	
                // 发现空闲目录项
                memcpy(dir_e + dir_entry_idx, p_de, dir_entry_size);    
                ide_write(cur_part->my_disk, all_blocks[block_idx], io_buf, 1);
                dir_inode->i_size += dir_entry_size;
                return true;
            }
            dir_entry_idx++;
        }
        block_idx++;
    }   
    printk("directory is full!\n");
    return false;
}

// 把分区part目录pdir中编号为inode_no的目录项删除
bool delete_dir_entry(struct partition *part, struct dir *pdir, uint32_t inode_no, void *io_buf) {
    struct inode *dir_inode = pdir->inode;
    uint32_t block_idx = 0, all_blocks[140] = {0};
    // 填充目录的数据块地址到all_blocks
    while (block_idx < 12) {
        all_blocks[block_idx] = dir_inode->i_sectors[block_idx];
        block_idx++;
    }
    if (dir_inode->i_sectors[12]) {
        ide_read(part->my_disk, dir_inode->i_sectors[12], all_blocks + 12, 1);
    }

    uint32_t dir_entry_size = part->sb->dir_entry_size;
    uint32_t dir_entrys_per_sec = (SECTOR_SIZE / dir_entry_size);
    struct dir_entry *dir_e = (struct dir_entry *)io_buf;
    struct dir_entry *dir_entry_found = NULL;
    uint8_t dir_entry_idx, dir_entry_cnt;
    bool is_dir_first_block = false;

    // 遍历目录对应的所有的数据块，寻找目录项
    block_idx = 0;
    while (block_idx < 140) {
        is_dir_first_block = false;
        if (all_blocks[block_idx] == 0) {
            block_idx++;
            continue;
        }
        dir_entry_idx = dir_entry_cnt = 0;
        memset(io_buf, 0, SECTOR_SIZE);
        // 读取数据块
        ide_read(part->my_disk, all_blocks[block_idx], io_buf, 1);
        // 遍历块中的目录项
        while (dir_entry_idx < dir_entrys_per_sec) {
            if ((dir_e + dir_entry_idx)->f_type != FT_UNKNOWN) {
                if (!strcmp((dir_e + dir_entry_idx)->filename, ".")) {
                    // .目录项所在的数据块肯定是该目录所有数据块的第一块
                    is_dir_first_block = true;
                } else if (strcmp((dir_e + dir_entry_idx)->filename, ".") && strcmp((dir_e + dir_entry_idx)->filename, "..")) {
                    // 除了.和..之外其他项的个数
                    dir_entry_cnt++;
                    if ((dir_e + dir_entry_idx)->i_no == inode_no) {
                        ASSERT(dir_entry_found == NULL);
                        dir_entry_found = dir_e + dir_entry_idx;
                    }
                }
            }
            dir_entry_idx++;
        }
        if (dir_entry_found == NULL) {
            // 如果当前数据块遍历完了没找到对应目录项，则继续遍历下一个数据块
            block_idx++;
            continue;
        }
        // 找到目录项，开始删除
        ASSERT(dir_entry_cnt >= 1);
        if (dir_entry_cnt == 1 && !is_dir_first_block) {
            // 如果当前数据块只有待删除的这一个目录项，且这个数据块不是.和..目录项所在的块，则删了目录项后回收这个块
            uint32_t block_bitmap_idx = all_blocks[block_idx] - part->sb->data_start_lba;
            // 修改位图相应的位为空闲状态
            bitmap_set(&part->block_bitmap, block_bitmap_idx, 0); 
            bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);
            // 清除目录对应inode中的块地址
            if (block_idx < 12) {
                dir_inode->i_sectors[block_idx] = 0;
            } else {
                uint32_t indirect_blocks = 0; 
                uint32_t indirect_block_idx = 12;
                // 查询间接表中有多少间接块
                while (indirect_block_idx < 140) { 
                    if (all_blocks[indirect_block_idx] != 0) { 
                        indirect_blocks++; 
                    } 
                }
                ASSERT(indirect_blocks >= 1); // 包括当前间接块
                if (indirect_blocks > 1) {
                    // 仅擦除间接表中需要回收的这一个数据块的地址
                    all_blocks[block_idx] = 0;
                    ide_write(part->my_disk, dir_inode->i_sectors[12], all_blocks + 12, 1);
                } else {
                    // 连间接表一起回收
                    block_bitmap_idx = dir_inode->i_sectors[12] - part->sb->data_start_lba;
                    bitmap_set(&part->block_bitmap, block_bitmap_idx, 0);
                    bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);
                    dir_inode->i_sectors[12] = 0;
                }
            }
        } else {
            // 仅清除该目录项
            memset(dir_entry_found, 0, dir_entry_size);
            ide_write(part->my_disk, all_blocks[block_idx], io_buf, 1);
        }
        // 更新该目录pdir的inode回硬盘
        ASSERT(dir_inode->i_size >= dir_entry_size);
        dir_inode->i_size -= dir_entry_size;
        memset(io_buf, 0, SECTOR_SIZE * 2);
        inode_sync(part, dir_inode, io_buf);

        return true;
    }
    return false;
}