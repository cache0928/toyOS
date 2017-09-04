[bits 32]
extern main
; 这是一个简易版的CRT
; 如果链接时候ld不指定-e main的话，那ld默认会使用_start来充当入口
; 这里的_start的简陋实现，充当了exec调用的进程从伪造的中断中返回时的入口地址
; 通过这个_start, 压入了在execv中存放用户进程参数的两个寄存器。然后call 用户进程main来实现了向用户进程传递参数
section .text 
global _start 
_start:
;下面这两个要和 execv 中 load 之后指定的寄存器一致 
    push ebx ;压入 argv 
    push ecx ;压入 argc 
    call main