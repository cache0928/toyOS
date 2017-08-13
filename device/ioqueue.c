#include "ioqueue.h"
#include "interrupt.h"
#include "global.h"
#include "debug.h"

void ioqueue_init(struct ioqueue *ioq) {
    lock_init(&ioq->lock);
    ioq->producer = ioq->consumer = NULL;
    ioq->head = ioq->tail = 0;
}

// 缓冲区头指针或者尾指针的下一个位置
static int32_t next_pos(int32_t pos) {
    return (pos + 1) % bufsize;
}

// 判断队列是否已满
// 队列的最大容量为bufsize - 1
bool ioq_full(struct ioqueue *ioq) {
    ASSERT(intr_get_status() == INTR_OFF);
    return next_pos(ioq->head) == ioq->tail;
}

// 判断队列是否已空
static bool ioq_empty(struct ioqueue *ioq) {
    ASSERT(intr_get_status() == INTR_OFF);
    return ioq->head == ioq->tail;
}

// 传入队列的生产者或者消费者的指针，让其等待
// 同一时间只能设置一个生产者或者消费者，所以要加锁
// 不然的话之前的生产者或者消费者阻塞，这里指向其的指针被替换掉之后就再也无法唤醒了
static void ioq_wait(struct task_struct **waiter) {
    ASSERT(*waiter == NULL && waiter != NULL);
    *waiter = running_thread();
    thread_block(TASK_BLOCKED);
}

// 唤醒waiter
static void wakeup(struct task_struct **waiter) {
    ASSERT(*waiter != NULL);
    thread_unblock(*waiter);
    *waiter = NULL;
}

// 消费者取一个char
char ioq_getchar(struct ioqueue *ioq) {
    ASSERT(intr_get_status() == INTR_OFF);
    while (ioq_empty(ioq)) {
        lock_acquire(&ioq->lock);
        ioq_wait(&ioq->consumer);
        lock_release(&ioq->lock);
    }
    char byte = ioq->buf[ioq->tail];
    ioq->tail = next_pos(ioq->tail);

    if (ioq->producer != NULL) {
        wakeup(&ioq->producer);
    }

    return byte;
}

// 生产者加一个数据
void ioq_putchar(struct ioqueue *ioq, char byte) {
    ASSERT(intr_get_status() == INTR_OFF);
    while (ioq_full(ioq)) {
        lock_acquire(&ioq->lock);
        ioq_wait(&ioq->producer);
        lock_release(&ioq->lock);
    }
    ioq->buf[ioq->head] = byte;
    ioq->head = next_pos(ioq->head);

    if (ioq->consumer != NULL) {
        wakeup(&ioq->consumer);
    }
}
