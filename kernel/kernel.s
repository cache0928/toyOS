[bits 32]
; 若在相关的异常中cpu已经自动压入了错误码,为保持栈中格式统一,这里不做操作.
%define ERROR_CODE nop
; 若在相关的异常中cpu没有压入错误码,为了统一栈中格式,就手工压入一个0
%define ZERO push 0

extern put_str ; 声明外部打印函数

section .data
intr_str db "interrupt occur!", 0xa, 0
; 编译之后，下面所有的.data会被排在一个segment中
; VECTOR宏每重复一次，就往其拥有的data section中添加一个中断处理程序的入口地址
; 最后合并在一起之后，intr_entry_table正好就是这个集合的第一个元素的地址，也就是C语言中的数组的地址
global intr_entry_table
intr_entry_table:
%macro VECTOR 2
section .text
intr%1entry:
    %2
    push intr_str
    call put_str
    add esp, 4 ; 跳过参数

    ; 如果是从片上进入的中断,除了往从片上发送EOI外,还要往主片上发送EOI 
    mov al, 0x20 ; 中断结束命令EOI
    out 0xa0, al ; 向从片发送
    out 0x20, al ; 向主片发送

    add esp, 4 ; 跨过error_code
    iret ; 从中断返回,32位下等同指令iretd
section .data
    dd intr%1entry	 ; 存储各个中断入口程序的地址，形成intr_entry_table数组
%endmacro

VECTOR 0x00, ZERO
VECTOR 0x01, ZERO
VECTOR 0x02, ZERO
VECTOR 0x03, ZERO 
VECTOR 0x04, ZERO
VECTOR 0x05, ZERO
VECTOR 0x06, ZERO
VECTOR 0x07, ZERO 
VECTOR 0x08, ERROR_CODE
VECTOR 0x09, ZERO
VECTOR 0x0a, ERROR_CODE
VECTOR 0x0b, ERROR_CODE 
VECTOR 0x0c, ZERO
VECTOR 0x0d, ERROR_CODE
VECTOR 0x0e, ERROR_CODE
VECTOR 0x0f, ZERO 
VECTOR 0x10, ZERO
VECTOR 0x11, ERROR_CODE
VECTOR 0x12, ZERO
VECTOR 0x13, ZERO 
VECTOR 0x14, ZERO
VECTOR 0x15, ZERO
VECTOR 0x16, ZERO
VECTOR 0x17, ZERO 
VECTOR 0x18, ERROR_CODE
VECTOR 0x19, ZERO
VECTOR 0x1a, ERROR_CODE
VECTOR 0x1b, ERROR_CODE 
VECTOR 0x1c, ZERO
VECTOR 0x1d, ERROR_CODE
VECTOR 0x1e, ERROR_CODE
VECTOR 0x1f, ZERO 
VECTOR 0x20, ZERO
