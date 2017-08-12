#include "sync.h"
#include "list.h"
#include "global.h"
#include "thread.h"
#include "interrupt.h"
#include "debug.h"

// 信号量初始化
void sema_init(struct semaphore *psema, uint8_t value) {
    psema->value = value;
    list_init(&psema->waiters);
}

// 锁初始化
void lock_init(struct lock *plock) {
    plock->holder = NULL;
    plock->holder_repeat_nr = 0;
    sema_init(&plock->semaphore, 1);
}

// 减少一个信号量
void sema_down(struct semaphore *psema) {
    enum intr_status old_status = intr_disable();
    while (psema->value == 0) {
        ASSERT(!elem_find(&psema->waiters, &running_thread()->general_tag));
        if (elem_find(&psema->waiters, &running_thread()->general_tag)) {
            PANIC("sema_down: thread blocked has been in waters_list\n");
        }
        list_append(&psema->waiters, &running_thread()->general_tag);
        thread_block(TASK_BLOCKED);
    }
    psema->value--;
    ASSERT(psema->value == 0);
    intr_set_status(old_status);
}

// 增加一个信号量
void sema_up(struct semaphore *psema) {
    enum intr_status old_status = intr_disable();
    ASSERT(psema->value == 0);
    if (!list_empty(&psema->waiters)) {
        struct task_struct *thread_blocked = elem2entry(struct task_struct, general_tag, list_pop(&psema->waiters));
        thread_unblock(thread_blocked);
    }
    psema->value++;
    ASSERT(psema->value == 1);
    intr_set_status(old_status);
}

// 获取锁
void lock_acquire(struct lock *plock) {
    if (plock->holder != running_thread()) {
        sema_down(&plock->semaphore);
        plock->holder = running_thread();
        ASSERT(plock->holder_repeat_nr == 0);
        plock->holder_repeat_nr = 1;
    } else {
        // 如果已经持有锁的线程重复申请锁，则光增加holder_repeat_nr而不减少信号量，否则就死锁了
        plock->holder_repeat_nr++;
    }
}

// 释放锁
void lock_release(struct lock *plock) {
    ASSERT(plock->holder == running_thread());
    if (plock->holder_repeat_nr > 1) {
        plock->holder_repeat_nr--;
        return;
    }
    ASSERT(plock->holder_repeat_nr == 1);
    plock->holder = NULL;
    plock->holder_repeat_nr = 0;
    sema_up(&plock->semaphore);
}