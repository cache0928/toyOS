#include "stdio.h"
#include "global.h"
#include "string.h"
#include "syscall.h"
// #define va_start(ap, v) ap = (va_list)&v 

// 将整形转换成字符
static void itoa(uint32_t value, char **buf_ptr_addr, uint8_t base) {
    uint32_t m = value % base;
    uint32_t i = value / base;
    if (i) {
        // 说明还没到头，还要继续取高位
        itoa(i, buf_ptr_addr, base);
    }
    if (m < 10) {
        // 余数是0-9
        *((*buf_ptr_addr)++) = m + '0';
    } else {
        *((*buf_ptr_addr)++) = m - 10 + 'A';
    }
}

// 将字符串按format格式展开，format中%对应的值用ap在栈中进行索引
uint32_t vsprintf(char *str, const char *format, va_list ap) {
    char *buf_ptr = str;
    const char *index_ptr = format;
    char index_char = *index_ptr;
    int32_t arg_int;
    char *arg_str;
    while (index_char) {
        if (index_char != '%') {
            // 原样拷贝过去
            *(buf_ptr++) = index_char;
            index_char = *(++index_ptr);
            continue;
        }
        // %后的字符
        index_char = *(++index_ptr);
        switch (index_char) {
            case 'x':
                arg_int = va_arg(ap, int);
                itoa(arg_int, &buf_ptr, 16);
                index_char = *(++index_ptr);
                break;
            case 's':
                arg_str = va_arg(ap, char *);
                strcpy(buf_ptr, arg_str);
                buf_ptr += strlen(arg_str);
                index_char = *(++index_ptr);
                break;
            case 'c':
                *(buf_ptr++) = va_arg(ap, char);
                index_char = *(++index_ptr);
                break;
            case 'd':
                arg_int = va_arg(ap, int);
                if (arg_int < 0) {
                    arg_int = 0 - arg_int;
                    *buf_ptr++ = '-';
                }
                itoa(arg_int, &buf_ptr, 10);
                index_char = *(++index_ptr);
                break;
        }
    }
    return strlen(str);
}

uint32_t printf(const char *format, ...) {
    va_list args;
    va_start(args);
    char buf[1024] = {0};
    vsprintf(buf, format, args);
    va_end(args);
    return write(buf);
}

uint32_t sprintf(char *buf, const char *format, ...) {
    va_list args;
    uint32_t retval;
    va_start(args);
    args+=4;
    retval = vsprintf(buf, format, args);
    va_end(args);
    return retval;
}