#ifndef __FS_FS_H
#define __FS_FS_H
#include "stdint.h"

#define MAX_FILES_PER_PART 4096 // 每个分区最大的文件数
#define BITS_PER_SECTOR 4096 // 每个扇区的bit数 512 * 8
#define SECTOR_SIZE 512 // 扇区大小 512字节
#define BLOCK_SIZE SECTOR_SIZE // 块大小，定为1扇区
#define MAX_PATH_LEN 512 // 最长路径

extern struct partition *cur_part; // 当前挂载的分区
enum file_types {
    FT_UNKNOWN, // 未知文件
    FT_REGULAR, // 普通文件
    FT_DIRECTORY // 目录
};
// 打开文件的选项
enum oflags {
    O_RDONLY, // 只读
    O_WRONLY, // 只写
    O_RDWR, // 读写
    O_CREATE = 4// 创建
};
// 文件操作偏移指针的参考量
enum whence {
    SEEK_SET = 1,
    SEEK_CUR,
    SEEK_END
};
struct dir;
// 文件查找记录
struct path_search_record {
    char searched_path[MAX_PATH_LEN]; // 当前查找的文件
    struct dir *parent_dir; // 当前查找文件的父目录
    enum file_types file_type; // 当前查找文件的类型
};

void filesys_init();
int32_t path_depth_cnt(char *pathname);
int32_t sys_open(const char *pathname, uint8_t flag);
int32_t sys_close(int32_t fd);
int32_t sys_write(int32_t fd, const void *buf, uint32_t count);
int32_t sys_read(int32_t fd, void *buf, uint32_t count);
int32_t sys_lseek(int32_t fd, int32_t offset, uint8_t whence);
int32_t sys_unlink(const char *pathname);
#endif