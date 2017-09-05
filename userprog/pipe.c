#include "pipe.h"
#include "memory.h"
#include "fs.h"
#include "file.h"
#include "ioqueue.h"
#include "thread.h"

// 重定向，将ld_local_fd重定向为new_local_fd，就是修改pcb中文件描述符表对应下标的内容
void sys_fd_redirect(uint32_t old_local_fd, uint32_t new_local_fd) {
    struct task_struct* cur = running_thread();
    if (new_local_fd < 3) {
        // 仅用于将标准描述符恢复为标准描述符时
        // 因为线程和进程刚初始化的时候，就已经默认将pcb文件描述符表中前3项分别赋值0，1，2对应全局文件表的前三项
        // 而函数get_free_slot_in_global从全局文件表获取空闲位的时候也是跨过前三项的
        // 所以按照原样恢复到线程／进程刚初始化时的样子即可达到目的
        cur->fd_table[old_local_fd] = new_local_fd;
    } else {
        // 其余的重定向就是更换一下对应下标的值，从而达到指向全局文件表中不同文件的目的
        uint32_t new_global_fd = cur->fd_table[new_local_fd];
        cur->fd_table[old_local_fd] = new_global_fd;
    }
}

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