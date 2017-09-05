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
#include "dir.h"
#include "file.h"
#include "console.h"
#include "keyboard.h"
#include "ioqueue.h"
#include "pipe.h"


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
    memcpy(p_de->filename, "..", 2);
    p_de->i_no = 0;
    p_de->f_type = FT_DIRECTORY;
    ide_write(hd, sb.data_start_lba, buf, 1);

    printk("   root_dir_lba:0x%x\n", sb.data_start_lba);
    printk("%s format done\n", part->name);
    sys_free(buf);
}

// 解析出最上层的路径名，存入到name_store中，返回剩下的路径名指针
char *path_parse(char *pathname, char *name_store) {
    if (pathname[0] == '/') {
        while (*(++pathname) == '/'); // 跨过开头的多个/
    }
    while (*pathname != '/' && *pathname != 0) {
        *name_store++ = *pathname++;
    }
    if(pathname[0] == 0) {
        // 已经到路径末尾，则返回NULL
        return NULL;
    }
    return pathname;
}

// 返回路径的级数 /a/b/c返回3
int32_t path_depth_cnt(char *pathname) {
    ASSERT(pathname != NULL);
    char *p = pathname;
    char name[MAX_FILE_NAME_LEN];
    uint32_t depth = 0;
    p = path_parse(p, name);
    while (name[0]) {
        depth++;
        memset(name, 0, MAX_FILE_NAME_LEN);
        if (p) {
            p = path_parse(p, name);
        }
    }
    return depth;
}

// 搜索绝对路径文件，找到返回去inode编号，失败返回-1
static int search_file(const char *pathname, struct path_search_record *searched_record) {
    // 如果查找的是根目录，直接返回根目录信息
    if (!strcmp(pathname, "/") || !strcmp(pathname, "/.") || !strcmp(pathname, "/..")) {
        searched_record->parent_dir = &root_dir;
        searched_record->file_type = FT_DIRECTORY;
        searched_record->searched_path[0] = 0;
        return 0;
    }
    // 确保是绝对路径, 如/a
    uint32_t path_len = strlen(pathname);
    ASSERT(pathname[0] == '/' && path_len > 1 && path_len < MAX_PATH_LEN);
    char *sub_path = (char *)pathname;
    struct dir *parent_dir = &root_dir;
    struct dir_entry dir_e;

    char name[MAX_FILE_NAME_LEN] = {0};

    searched_record->parent_dir = &root_dir;
    searched_record->file_type = FT_UNKNOWN;
    uint32_t parent_inode_no = 0;

    // 开始查找
    sub_path = path_parse(sub_path, name);
    while (name[0]) {
        ASSERT(strlen(searched_record->searched_path) < 512);
        strcat(searched_record->searched_path, "/");
        strcat(searched_record->searched_path, name);
        // 在当前目录中查找name文件
        if (search_dir_entry(cur_part, parent_dir, name, &dir_e)) {
            memset(name, 0, MAX_FILE_NAME_LEN);
            if (sub_path) {
                // 还未到达最后一级，继续拆分
                sub_path = path_parse(sub_path, name);
            }
            if (dir_e.f_type == FT_DIRECTORY) {
                // 当前查找结果是目录, 则接下来在这个目录中继续寻找下一个name文件
                parent_inode_no = parent_dir->inode->i_no;
                dir_close(parent_dir);
                parent_dir = dir_open(cur_part, dir_e.i_no);
                searched_record->parent_dir = parent_dir;
                continue;
            } else if (dir_e.f_type == FT_REGULAR) {
                // 当前查找结果是普通文件
                searched_record->file_type = FT_REGULAR;
                return dir_e.i_no;
            }
        } else {
            // 没找到对应的name文件，无论目录还是普通文件
            return -1;
        }
    }
    // 对应最终的查找结果不是普通文件，而是目录
    dir_close(searched_record->parent_dir);
    // 重置查找结果的父目录
    searched_record->parent_dir = dir_open(cur_part, parent_inode_no);
    searched_record->file_type = FT_DIRECTORY;
    return dir_e.i_no;
}

// 根据flag打开或创建文件，成功则返回文件描述符
int32_t sys_open(const char *pathname, uint8_t flag) {
    if (pathname[strlen(pathname)-1] == '/') {
        printk("can`t open a directory %s\n",pathname);
        return -1;
    }
    ASSERT(flag < 7);
    int32_t fd = -1;

    struct path_search_record searched_record;
    memset(&searched_record, 0, sizeof(struct path_search_record));

    uint32_t pathname_depth = path_depth_cnt((char *)pathname);

    int inode_no = search_file(pathname, &searched_record);
    bool found = inode_no != -1 ? true : false;

    if (searched_record.file_type == FT_DIRECTORY) {
        // 找到的是目录
        printk("can't open a direcotry with open(), use opendir() to instead\n");
        dir_close(searched_record.parent_dir);
        return -1;
    }

    uint32_t path_searched_depth = path_depth_cnt(searched_record.searched_path);

    if (pathname_depth != path_searched_depth) {
        // 没有访问到最后，中间某一级目录不存在
        printk("cannot access %s: Not a directory, subpath %s is’t exist\n", pathname, searched_record.searched_path);
        dir_close(searched_record.parent_dir);
        return -1;
    }

    // 所有路径都访问到了
    if (!found && !(flag & O_CREATE)) {
        // 没有这个文件，且标记中没有给出创建
        printk("in path %s, file %s isn't exist\n", searched_record.searched_path, (strrchr(searched_record.searched_path, '/') + 1));
        dir_close(searched_record.parent_dir);
        return -1;
    } else if (found && (flag & O_CREATE)) {
        // 找到了同名文件，且标记给出了创建
        printk("%s has already exist!\n", pathname);
        dir_close(searched_record.parent_dir);
        return -1;
    }

    switch (flag & O_CREATE) {
        case O_CREATE:
            // 创建文件
            printk("creating file\n");
            fd = file_create(searched_record.parent_dir, (strrchr(pathname, '/') + 1), flag);
            dir_close(searched_record.parent_dir);
            break;
        default:
            // 打开文件
            fd = file_open(inode_no, flag);
    }
    return fd;
}

// 将文件描述符转化成对应的文件表下标
uint32_t fd_local2global(uint32_t local_fd) {
    struct task_struct *cur = running_thread();
    int32_t global_fd = cur->fd_table[local_fd];
    ASSERT(global_fd >= 0 && global_fd < MAX_FILE_OPEN);
    return (uint32_t)global_fd;
}

// 关闭文件描述符fd对应的文件，成功返回0，失败返回-1
int32_t sys_close(int32_t fd) {
    int32_t ret = -1;
    if (fd > 2) {
        uint32_t gfd = fd_local2global(fd);
        if (is_pipe(fd)) {
            if (--file_table[gfd].fd_pos == 0) {
                // 回收管道
                mfree_page(PF_KERNEL, file_table[gfd].fd_inode, 1);
                file_table[gfd].fd_inode = NULL;
            }
        } else {
            ret = file_close(&file_table[gfd]);            
        }
        running_thread()->fd_table[fd] = -1;
    }
    return ret;
}

// 将buf中连续count字节的内容写入到文件描述符fd，成功返回字节数，失败返回-1
int32_t sys_write(int32_t fd, const void *buf, uint32_t count) {
    if (fd < 0) {
        printk("sys_write: fd error\n");
    }
    if (fd == stdout_no) {
        if (is_pipe(fd)) {
            return pipe_write(fd, buf, count);
        } else {
            // 标准输出就是往控制台打印信息
            char tmp_buf[1024] = {0};
            memcpy(tmp_buf, buf, count);
            console_put_str(tmp_buf);
            return count;
        }
    } else if (is_pipe(fd)) {
        return pipe_write(fd, buf, count);
    } else {
        uint32_t _fd = fd_local2global(fd);
        struct file *file = &file_table[_fd];
        if (file->fd_flag & O_WRONLY || file->fd_flag & O_RDWR) {
            // 可写
            uint32_t bytes_written = file_write(file, buf, count);
            return bytes_written;
        } else {
            console_put_str("sys_write: not allowed to write file without flag O_RDWR or O_WRONLY\n");
            return -1;
        }
    }
}

// 从文件描述符fd指向的文件中读出count字节到buf，成功返回读出的字节数，失败返回-1
int32_t sys_read(int32_t fd, void *buf, uint32_t count) {
    ASSERT(buf != NULL);
    int32_t ret = -1;
    if (fd < 0 || fd == stdout_no || fd == stderr_no) {
        printk("sys_read: fd error\n");
    } else if (fd == stdin_no) {
        if (is_pipe(fd)) {
            ret = pipe_read(fd, buf, count);
        } else {
            char *buffer = buf;
            uint32_t bytes_read = 0;
            while (bytes_read < count) {
                *buffer = ioq_getchar(&kbd_buf);
                bytes_read++;
                buffer++;
            }
            ret = (bytes_read == 0 ? -1 : (int32_t)bytes_read);
        }
    } else if (is_pipe(fd)) {
        ret = pipe_read(fd, buf, count);
    } else {
        uint32_t _fd = fd_local2global(fd);
        ret = file_read(&file_table[_fd], buf, count);
    }
    return ret;
}

// 重置文件的操作偏移指针，成功返回新的偏移量，失败返回-1
int32_t sys_lseek(int32_t fd, int32_t offset, uint8_t whence) {
    if (fd < 0) {
        printk("sys_lseek: fd error\n");
        return -1;
    }
    ASSERT(whence > 0 && whence < 4);
    uint32_t _fd = fd_local2global(fd);
    struct file *file = &file_table[_fd];
    int32_t new_pos = 0;
    int32_t file_size = (int32_t)file->fd_inode->i_size;
    switch (whence) {
        case SEEK_SET:
            new_pos = offset;
            break;
        case SEEK_CUR:
            new_pos = (int32_t)file->fd_pos + offset;
            break;
        case SEEK_END:
            new_pos = file_size + offset;
    }
    if (new_pos < 0 || new_pos > (file_size - 1)) {
        return -1;
    }
    file->fd_pos = new_pos;
    return file->fd_pos;
}

// 删除文件，成功返回0，失败返回-1
int32_t sys_unlink(const char *pathname) {
    ASSERT(strlen(pathname) < MAX_PATH_LEN);
    // 查找待删除文件的inode
    struct path_search_record searched_record;
    memset(&searched_record, 0, sizeof(struct path_search_record));
    int inode_no = search_file(pathname, &searched_record);
    ASSERT(inode_no != 0);
    if (inode_no == -1) {
        printk("file %s not found!\n", pathname); 
        dir_close(searched_record.parent_dir); 
        return -1;
    }
    if (searched_record.file_type == FT_DIRECTORY) {
        printk("can`t delete a direcotry with unlink(), use rmdir() to instead\n");
        dir_close(searched_record.parent_dir);
        return -1;
    }
    // 检查待删除文件当前是否已经打开，如果有打开不能删除
    uint32_t file_idx = 0;
    while (file_idx < MAX_FILE_OPEN) {
        if (file_table[file_idx].fd_inode != NULL && (uint32_t)inode_no == file_table[file_idx].fd_inode->i_no) {
            break;
        }
        file_idx++;
    }
    if (file_idx < MAX_FILE_OPEN) {
        dir_close(searched_record.parent_dir);
        printk("file %s is in use, not allow to delete!\n", pathname);
        return -1;
    }
    ASSERT(file_idx == MAX_FILE_OPEN);
    void* io_buf = sys_malloc(SECTOR_SIZE + SECTOR_SIZE);
    if (io_buf == NULL) {
        dir_close(searched_record.parent_dir);
        printk("sys_unlink: malloc for io_buf failed\n");
        return -1;
    }

    struct dir* parent_dir = searched_record.parent_dir;
    // 先删除目录项
    delete_dir_entry(cur_part, parent_dir, inode_no, io_buf);
    // 释放对应的inode
    inode_release(cur_part, inode_no);
    sys_free(io_buf);
    dir_close(searched_record.parent_dir);
    return 0;
}

// 创建目录pathname，成功返回0，失败返回-1
int32_t sys_mkdir(const char *pathname) {
    uint8_t rollback_step = 0;
    void *io_buf = sys_malloc(SECTOR_SIZE * 2);
    if (io_buf == NULL) {
        printk("sys_mkdir: sys_malloc for io_buf failed\n");
        return -1;
    }

    struct path_search_record searched_record;
    memset(&searched_record, 0, sizeof(struct path_search_record));
    int inode_no = -1;
    inode_no = search_file(pathname, &searched_record);
    if (inode_no != -1) {
        // 存在同名文件或文件夹
        printk("sys_mkdir: file or directory %s exist!\n", pathname);
        rollback_step = 1;
        goto rollback;
    } else {
        uint32_t pathname_depth = path_depth_cnt((char *)pathname);
        uint32_t path_searched_depth = path_depth_cnt(searched_record.searched_path);
        if (pathname_depth != path_searched_depth) {
            // pathname中某一级文件夹不存在
            printk("sys_mkdir: cannot access %s: Not a directory,subpath %s isn't exist\n", pathname, searched_record.searched_path);
            rollback_step = 1;
            goto rollback;
        }
    }
    struct dir *parent_dir = searched_record.parent_dir;
    char *dirname = strrchr(searched_record.searched_path, '/') + 1;
    // 给新目录分配inode
    inode_no = inode_bitmap_alloc(cur_part);
    if (inode_no == -1) {
        printk("sys_mkdir: allocate inode failed\n");
        rollback_step = 1;
        goto rollback;
    }
    struct inode new_dir_inode;
    inode_init(inode_no, &new_dir_inode);
    // 给新目录分配一个数据块，放入.和..两个目录项
    uint32_t block_bitmap_idx = 0;
    int32_t block_lba = -1;
    block_lba = block_bitmap_alloc(cur_part);
    if (block_lba == -1) {
        printk("sys_mkdir: block_bitmap_alloc for create directory failed\n");
        rollback_step = 2;
        goto rollback;
    }
    new_dir_inode.i_sectors[0] = block_lba;
    // 及时同步块位图到硬盘
    block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;
    ASSERT(block_bitmap_idx != 0);
    bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);
    // 写入目录项.和..
    memset(io_buf, 0, SECTOR_SIZE * 2);
    struct dir_entry *p_de = (struct dir_entry *)io_buf;
    // .
    memcpy(p_de->filename, ".", 1);
    p_de->i_no = inode_no;
    p_de->f_type = FT_DIRECTORY;
    p_de++;
    // ..
    memcpy(p_de->filename, "..", 2);
    p_de->i_no = parent_dir->inode->i_no;
    p_de->f_type = FT_DIRECTORY;
    ide_write(cur_part->my_disk, new_dir_inode.i_sectors[0], io_buf, 1);

    new_dir_inode.i_size = 2 * cur_part->sb->dir_entry_size;

    // 在父目录中添加新目录的目录项
    struct dir_entry new_dir_entry;
    memset(&new_dir_entry, 0, sizeof(struct dir_entry));
    create_dir_entry(dirname, inode_no, FT_DIRECTORY, &new_dir_entry);
    memset(io_buf, 0, SECTOR_SIZE * 2);
    if (!sync_dir_entry(parent_dir, &new_dir_entry, io_buf)) {
        printk("sys_mkdir: sync_dir_entry to disk failed!\n");
        rollback_step = 2;
        goto rollback;
    }

    // 将各个修改后的inode同步到硬盘
    memset(io_buf, 0, SECTOR_SIZE * 2); 
    inode_sync(cur_part, parent_dir->inode, io_buf);
    memset(io_buf, 0, SECTOR_SIZE * 2); 
    inode_sync(cur_part, &new_dir_inode, io_buf);
    bitmap_sync(cur_part, inode_no, INODE_BITMAP);
    // 回收资源
    sys_free(io_buf);
    dir_close(searched_record.parent_dir);
    return 0;

rollback:
    switch (rollback_step) {
        case 2:
            bitmap_set(&cur_part->inode_bitmap, inode_no, 0);
        case 1:
            dir_close(searched_record.parent_dir);
            break;
    }
    sys_free(io_buf);
    return -1;
}

// 打开目录，成功返回dir指针
struct dir *sys_opendir(const char *name) {
    ASSERT(strlen(name) < MAX_PATH_LEN);
    if (!strcmp(name, "/") || !strcmp(name, "/.") || !strcmp(name, "/..")) {
        // 返回根目录
        return &root_dir;
    }
    struct path_search_record searched_record;
    memset(&searched_record, 0, sizeof(struct path_search_record));
    int inode_no = search_file(name, &searched_record);
    struct dir *ret = NULL;
    if (inode_no == -1) {
        // 目录不存在
        printk("In %s, sub path %s not exist\n", name, searched_record.searched_path);
    } else {
        if (searched_record.file_type == FT_REGULAR) {
            printk("%s is regular file!\n", name);
        } else if (searched_record.file_type == FT_DIRECTORY) {
            ret = dir_open(cur_part, inode_no);
        }
    }
    dir_close(searched_record.parent_dir);
    return ret;
}

// 关闭目录，成功返回0，失败返回-1
int32_t sys_closedir(struct dir *dir) {
    int32_t ret = -1;
    if (dir != NULL) {
        dir_close(dir);
        ret = 0;
    }
    return ret;
}

// 读取目录的一个目录项，成功返回目录项指针
struct dir_entry *sys_readdir(struct dir *dir) {
    ASSERT(dir != NULL);
    return dir_read(dir);
}

// 重置目录的dir_pos指针
void sys_rewinddir(struct dir *dir) {
    dir->dir_pos = 0;
}

// 删除目录，目录需为空
int32_t sys_rmdir(const char *pathname) {
    struct path_search_record searched_record;
    memset(&searched_record, 0, sizeof(struct path_search_record)); 
    int inode_no = search_file(pathname, &searched_record);
    ASSERT(inode_no != 0);
    int retval = -1;
    if (inode_no == -1) {
        // 目录不存在
        printk("In %s, sub path %s not exist\n", pathname, searched_record.searched_path);
    } else {
        if (searched_record.file_type == FT_REGULAR) {
            // 路径对应的是普通文件而不是目录
            printk("%s is regular file!\n", pathname);
        } else {
            struct dir* dir = dir_open(cur_part, inode_no);
            if (!dir_is_empty(dir)) {
                printk("dir %s is not empty, it is not allowed to delete a nonempty directory!\n", pathname);
            } else {
                if (!dir_remove(searched_record.parent_dir, dir)) {
                    retval = 0;
                }
            }
            dir_close(dir);
        }
    }
    dir_close(searched_record.parent_dir);
    return retval;
}

// 获取父目录的inode序号
static uint32_t get_parent_dir_inode_nr(uint32_t child_inode_nr, void *io_buf) {
    struct inode *child_dir_inode = inode_open(cur_part, child_inode_nr);
    // ..项在目录数据块的第0块的第1项
    uint32_t block_lba = child_dir_inode->i_sectors[0];
    ASSERT(block_lba >= cur_part->sb->data_start_lba);
    inode_close(child_dir_inode);
    // 读入第0块
    ide_read(cur_part->my_disk, block_lba, io_buf, 1);
    struct dir_entry *dir_e = (struct dir_entry *)io_buf;
    ASSERT(dir_e[1].i_no < 4096 && dir_e[1].f_type == FT_DIRECTORY);
    return dir_e[1].i_no;
}

// 在inode编号为p_inode的目录中查找编号为c_inode_nr的字目录名称，结果放入path
static int get_child_dir_name(uint32_t p_inode_nr, uint32_t c_inode_nr, char *path, void *io_buf) {
    struct inode *parent_dir_inode = inode_open(cur_part, p_inode_nr);
    uint8_t block_idx = 0;
    uint32_t all_blocks[140] = {0};
    uint32_t block_cnt = 12;
    // 填充目录对应所有的数据块扇区地址到all_blocks
    while (block_idx < 12) {
        // 直接块
        all_blocks[block_idx] = parent_dir_inode->i_sectors[block_idx];
        block_idx++;
    }
    if (parent_dir_inode->i_sectors[12] != 0) {
        // 间接块
        ide_read(cur_part->my_disk, parent_dir_inode->i_sectors[12], all_blocks + 12, 1);
        block_cnt = 140;
    }
    inode_close(parent_dir_inode);
    // 遍历所有块，寻找对应c_inode_nr的目录项
    struct dir_entry *dir_e = (struct dir_entry*)io_buf; 
    uint32_t dir_entry_size = cur_part->sb->dir_entry_size; 
    uint32_t dir_entrys_per_sec = (512 / dir_entry_size);
    block_idx = 0;
    while (block_idx < block_cnt) {
        if (all_blocks[block_idx]) {
            ide_read(cur_part->my_disk, all_blocks[block_idx], io_buf, 1);
            uint8_t dir_e_idx = 0;
            // 遍历块中的目录项
            while (dir_e_idx < dir_entrys_per_sec) {
                if ((dir_e_idx + dir_e)->i_no == c_inode_nr) {
                    strcat(path, "/");
                    strcat(path, (dir_e + dir_e_idx)->filename);
                    return 0;
                }
                dir_e_idx++;
            }
        }
        block_idx++;
    }
    return -1;
}

// 将当前工作目录的绝对路径写入buf，size是buf的大小
char *sys_getcwd(char *buf, uint32_t size) {
    ASSERT(buf != NULL);
    void *io_buf = sys_malloc(SECTOR_SIZE);
    if (io_buf == NULL) {
        return NULL;
    }

    struct task_struct *cur_thread = running_thread();
    int32_t parent_inode_nr = 0;
    int32_t child_inode_nr = cur_thread->cwd_inode_nr;
    ASSERT(child_inode_nr >= 0 && child_inode_nr < 4096);
    if (child_inode_nr == 0) {
        // 如果是根目录直接返回/
        buf[0] = '/';
        buf[1] = 0;
        return buf;
    }
    memset(buf, 0, size);
    char full_path_r[MAX_PATH_LEN] = {0}; // 最终获取的路径的倒置
    
    // 从当前线程的cwd_inode_nr开始逐级往上查找目录名并拼接，直到根目录
    while (child_inode_nr) {
        parent_inode_nr = get_parent_dir_inode_nr(child_inode_nr, io_buf);
        if (get_child_dir_name(parent_inode_nr, child_inode_nr, full_path_r, io_buf) == -1) {
            // 找不到名字，失败
            sys_free(io_buf);
            return NULL;
        }
        child_inode_nr = parent_inode_nr;
    }

    // 已经获取到最终路径的倒置，开始调整
    ASSERT(strlen(full_path_r) <= size);
    char *last_slash; // 最后一个／的位置
    while ((last_slash = strrchr(full_path_r, '/'))) {
        uint16_t len = strlen(buf);
        strcpy(buf + len, last_slash);
        *last_slash = 0;
    }
    sys_free(io_buf);
    return buf;
}

// 改变当前线程的工作目录为path，成功返回0，失败返回-1
int32_t sys_chdir(const char *path) {
    int32_t ret = -1;
    struct path_search_record searched_record;
    memset(&searched_record, 0, sizeof(struct path_search_record));
    int inode_no = search_file(path, &searched_record);
    if (inode_no != -1) {
        if (searched_record.file_type == FT_DIRECTORY) {
            running_thread()->cwd_inode_nr = inode_no;
            ret = 0;
        } else {
            printk("sys_chdir: %s is regular file or other!\n", path);
        }
    }
    dir_close(searched_record.parent_dir);
    return ret;
}

// 在buf中填充path对应的文件信息，成功返回0，失败返回-1
int32_t sys_stat(const char *path, struct stat *buf) {
    if (!strcmp(path, "/") || !strcmp(path, "/.") || !strcmp(path, "/..")) {
        // 根目录
        buf->st_filetype = FT_DIRECTORY;
        buf->st_ino = 0;
        buf->st_size = root_dir.inode->i_size;
        return 0;
    }
    int32_t ret = -1;
    struct path_search_record searched_record;
    memset(&searched_record, 0, sizeof(struct path_search_record));
    int inode_no = search_file(path, &searched_record);
    if (inode_no != -1) {
        struct inode *obj_inode = inode_open(cur_part, inode_no);
        buf->st_size = obj_inode->i_size;
        inode_close(obj_inode);
        buf->st_filetype = searched_record.file_type;
        buf->st_ino = inode_no;
        ret = 0;
    } else {
        printk("sys_stat: %s not found\n", path);
    }
    dir_close(searched_record.parent_dir);
    return ret;
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
                    ide_read(hd, part->start_lba + 1, sb_buf, 1);
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

    open_root_dir(cur_part);

    // 初始化全局文件表
    uint32_t fd_idx = 0;
    while (fd_idx < MAX_FILE_OPEN) {
        file_table[fd_idx++].fd_inode = NULL;
    }
}

void sys_help() {
    printk("\
  buildin commands:\n\
        ls: show directory or file information\n\
        cd: change current work directory\n\
        mkdir: create a directory\n\
        rmdir: remove a empty directory\n\
        rm: remove a regular file\n\
        pwd: show current work directory\n\
        ps: show process information\n\
        clear: clear screen\n\n");
}