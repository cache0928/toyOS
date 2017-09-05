#include "pipe.h"
#include "memory.h"
#include "fs.h"
#include "file.h"
#include "ioqueue.h"
#include "thread.h"

// 判断文件描述符local_Fd是否为管道
bool is_pipe(uint32_t local_fd) {
    uint32_t global_fd = fd_local2global(local_fd);
    return file_table[global_fd].fd_flag == PIPE_FLAG;
}

// 创建管道，成功返回0，失败返回-1
int32_t sys_pipe(int32_t pipefd[2]) {
    int32_t global_fd = get_free_slot_in_global();
    // 申请环形缓冲区，初始化
    file_table[global_fd].fd_inode = get_kernel_pages(1);
    ioqueue_init((struct ioqueue *)file_table[global_fd].fd_inode);
    if (file_table[global_fd].fd_inode == NULL) {
        return -1;
    }
    // 设置标志
    file_table[global_fd].fd_flag = PIPE_FLAG;
    // 设置管道当前打开数
    file_table[global_fd].fd_pos = 2;
    pipefd[0] = pcb_fd_install(global_fd);
    pipefd[1] = pcb_fd_install(global_fd);
    return 0;
}

/*管道读写，为防止阻塞，最多读写环形缓冲区大小-1的数据，这是一个缺陷*/
// 从管道中读取数据
uint32_t pipe_read(int32_t fd, void *buf, uint32_t count) {
    char *buffer = buf;
    uint32_t bytes_read = 0;
    uint32_t global_fd = fd_local2global(fd);
    struct ioqueue *ioq = (struct ioqueue *)file_table[global_fd].fd_inode;

    uint32_t ioq_len = ioq_length(ioq);
    uint32_t size = ioq_len > count ? count : ioq_len;
    while (bytes_read < size) {
        *buffer = ioq_getchar(ioq);
        bytes_read++;
        buffer++;
    }
    return bytes_read;
}

// 向管道中写入数据
uint32_t pipe_write(int32_t fd, const void* buf, uint32_t count) {
    uint32_t bytes_write = 0;
    uint32_t global_fd = fd_local2global(fd);
    struct ioqueue *ioq = (struct ioqueue *)file_table[global_fd].fd_inode;

    /* 选择较小的数据写入量,避免阻塞 */
    uint32_t ioq_left = bufsize - ioq_length(ioq);
    uint32_t size = ioq_left > count ? count : ioq_left;

    const char *buffer = buf;
    while (bytes_write < size) {
        ioq_putchar(ioq, *buffer);
        bytes_write++;
        buffer++;
    }
    return bytes_write;
}