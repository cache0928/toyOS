#ifndef __DEVICE_IOQUEUE_H
#define __DEVICE_IOQUEUE_H
#include "stdint.h"
#include "thread.h"
#include "sync.h"

#define bufsize 64

struct ioqueue {
    // 争当消费者或者生产者的时候要加锁
    struct lock lock;
    struct task_struct *producer;
    struct task_struct *consumer;

    char buf[bufsize];
    int32_t head; // 头指针，插入数据
    int32_t tail; // 尾指针，读取数据
};

void ioqueue_init(struct ioqueue *ioq);
bool ioq_full(struct ioqueue *ioq);
void ioq_putchar(struct ioqueue *ioq, char byte);
char ioq_getchar(struct ioqueue *ioq);

#endif