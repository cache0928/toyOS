#ifndef __KERNEL_INTERRUPT_H
#define __KERNEL_INTERRUPT_H
#include "stdint.h"
typedef void * intr_handler;
void idt_init(void);
enum intr_status {		 // 中断状态
    INTR_OFF,			 // 中断关闭
    INTR_ON		         // 中断打开
};
enum intr_status intr_get_status();
enum intr_status intr_set_status (enum intr_status);
enum intr_status intr_enable ();
enum intr_status intr_disable ();
void register_handler(uint8_t vector_no, intr_handler function);
#endif
