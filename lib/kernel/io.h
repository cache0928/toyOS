#ifndef __LIB_IO_H
#define __LIB_IO_H
#include "stdint.h"

// 统一写在头文件，且用inline修饰，是为了调用的地方直接在编译时展开，避免call，提高效率

/* 向端口port写入一个字节*/
static inline void outb(uint16_t port, uint8_t data) {
    asm volatile ("outb %b0, %w1" : : "a"(data), "Nd"(port));    
}

/* 将addr处起始的word_cnt个字写入端口port */
static inline void outsw(uint16_t port, const void *addr, uint32_t word_cnt) {
    // outsw是把ds:esi处的16位的内容写入port端口
    asm volatile ("cld; rep outsw" : "+S"(addr), "+c"(word_cnt) : "d"(port));
}

/* 将从端口port读入的一个字节返回 */
static inline uint8_t inb(uint16_t port) {
    uint8_t data;
    asm volatile ("inb %w1, %b0" : "=a"(data) : "Nd"(port));
    return data;
}

/* 将从端口port读入的word_cnt个字写入addr */
static inline void insw(uint16_t port, void *addr, uint32_t word_cnt) {
    // insw是将从端口port处读入的16位内容写入es:edi指向的内存
    asm volatile ("cld; rep insw" : "+D"(addr), "+c"(word_cnt) : "d"(port) : "memory");
}

#endif