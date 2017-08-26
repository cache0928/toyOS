#ifndef __FS_FS_H
#define __FS_FS_H

#define MAX_FILES_PER_PART 4096 // 每个分区最大的文件数
#define BITS_PER_SECTOR 4096 // 每个扇区的bit数 512 * 8
#define SECTOR_SIZE 512 // 扇区大小 512字节
#define BLOCK_SIZE SECTOR_SIZE // 块大小，定为1扇区

enum file_types {
    FT_UNKNOWN, // 未知文件
    FT_REGULAR, // 普通文件
    FT_DIRECTORY // 目录
};
void filesys_init();
#endif