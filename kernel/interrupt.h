#ifndef __KERNEL_INTERRUPT_H
#define __KERNEL_INTERRUPT_H
#include "stdint.h"
typedef void * intr_handler;
void idt_init(void);
// 中断状态
enum intr_status {
    INTR_OFF,
    INTR_ON
};
enum intr_status intr_get_status();
enum intr_status intr_set_status(enum intr_status status);
enum intr_status intr_enable();
enum intr_status intr_disable();
void register_handler(uint8_t vector_no, intr_handler function);
#endif