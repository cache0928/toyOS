#include "timer.h"
#include "io.h"
#include "print.h"
#include "stdint.h"
#include "thread.h"
#include "interrupt.h"
#include "debug.h"

#define IRQ0_FREQUENCY 100
#define INPUT_FREQUENCY 1193180
#define COUNTER0_VALUE (INPUT_FREQUENCY / IRQ0_FREQUENCY)
#define COUNTER0_PORT 0x40
#define COUNTER0_NO 0
#define COUNTER_MODE 2
#define READ_WRITE_LATCH 3
#define PIT_CONTROL_PORT 0x43
// 设置定时器的编号、读写锁属性rwl、工作模式counter_mode，并设置初始值counter_value
static void frequency_set(uint8_t counter_port, uint8_t counter_no, uint8_t rwl, uint8_t counter_mode, uint16_t counter_value) {
    // 模式控制字
    outb(PIT_CONTROL_PORT, (uint8_t)(counter_no << 6 | rwl << 4 | counter_mode << 1));
    // 计数初始值的低8位
    outb(counter_port, (uint8_t)counter_value);
    // 计数初始值的高8位
    outb(counter_port, (uint8_t)(counter_value >> 8));
}


uint32_t ticks; // 内核自中断开启以后总的嘀嗒数

// 时钟中断的处理函数
static void intr_timer_handler() {
    struct task_struct *cur_thread = running_thread();
    ASSERT(cur_thread->stack_magic == 0x19920928); // 检查栈是否溢出
    cur_thread->elapsed_ticks++;
    ticks++;
    if (cur_thread->ticks == 0) {
        // 时间已到，该被换下了
        schedule();
    } else {
        cur_thread->ticks--;
    }
}

// 初始化定时器8253
void timer_init() {
    put_str("timer_init start\n");
    frequency_set(COUNTER0_PORT, COUNTER0_NO, READ_WRITE_LATCH, COUNTER_MODE, COUNTER0_VALUE);
    register_handler(0x20, intr_timer_handler);
    put_str("timer_init done\n");
}
