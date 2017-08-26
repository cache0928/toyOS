#ifndef __FS_DIR_H
#define __FS_DIR_H

#include "inode.h"
#include "fs.h"
#define MAX_FILE_NAME_LEN 16 // 最长文件名

struct dir {
    struct inode *inode;
    uint32_t dir_pos;
    uint8_t dir_buf[512];
};

// 目录项
struct dir_entry {
    char filename[MAX_FILE_NAME_LEN]; // 文件名
    uint32_t i_no;
    enum file_types f_type; // 文件类型
};

#endif