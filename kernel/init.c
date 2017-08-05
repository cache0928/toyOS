#include "init.h"
#include "print.h"
#include "interrupt.h"
#include "timer.h"

void init_all() {
    put_str("init_all\n");
    idt_init(); // 初始化中断
    timer_init(); // 初始化定时器8253
}