#ifndef __LIB_STDIO_H
#define __LIB_STDIO_H
#include "stdint.h"
#include "global.h"
// 可变参数的原理就是通过寻找format字符串中的%，通过栈指针来获取对应的栈中的参数
typedef char * va_list;
#define va_start(ap) asm volatile ("movl %%ebp, %0" : : "m"(ap) : "memory"); ap += 8 // 将ap指向第一个固定参数v
#define va_arg(ap, t) *((t *)(ap += 4)) // ap指向下一个参数，并按t所代表的类型返回其值
#define va_end(ap) ap = NULL // 清除ap

uint32_t vsprintf(char *str, const char *format, va_list ap);
uint32_t printf(const char *format, ...);
uint32_t sprintf(char *buf, const char *format, ...);
#endif