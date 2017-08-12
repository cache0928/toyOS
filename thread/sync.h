#ifndef __THREAD_SYNC_H
#define __THREAD_SYNC_H
#include "list.h"
#include "stdint.h"
#include "thread.h"

struct semaphore {
    uint8_t value;
    // 等待拿锁的线程
    struct list waiters;
};

struct lock {
    struct task_struct *holder; // 持有者
    struct semaphore semaphore; // 信号量
    uint32_t holder_repeat_nr; // 持有者申请锁的次数
};

void lock_init(struct lock *plock);
void lock_release(struct lock *plock);
void lock_acquire(struct lock *plock);

#endif