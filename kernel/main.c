#include "print.h"
#include "init.h"

int main() {
    put_str("my_os's kernel start\n");
    init_all();
    asm volatile ("sti"); // 临时打开中断
    while(1);
    return 0;
}

