[bits 32]
; 若在相关的异常中cpu已经自动压入了错误码,为保持栈中格式统一,这里不做操作.
%define ERROR_CODE nop
; 若在相关的异常中cpu没有压入错误码,为了统一栈中格式,就手工压入一个0
%define ZERO push 0

extern put_str ; 声明外部打印函数
extern idt_table

section .data
; 编译之后，下面所有的.data会被排在一个segment中
; VECTOR宏每重复一次，就往其拥有的data section中添加一个中断处理程序的入口地址
; 最后合并在一起之后，intr_entry_table正好就是这个集合的第一个元素的地址，也就是C语言中的数组的地址
global intr_entry_table
intr_entry_table:
%macro VECTOR 2
section .text
intr%1entry:
    %2
    ; 保存上下文
    push ds
    push es
    push fs
    push gs
    pushad

    ; 如果是从片上进入的中断,除了往从片上发送EOI外,还要往主片上发送EOI 
    mov al, 0x20 ; 中断结束命令EOI
    out 0xa0, al ; 向从片发送
    out 0x20, al ; 向主片发送

    push %1
    call [idt_table + %1*4]
    jmp intr_exit
section .data
    dd intr%1entry	 ; 存储各个中断入口程序的地址，形成intr_entry_table数组
%endmacro

section .text
global intr_exit
intr_exit:
    ; 恢复上下文
    add esp, 4
    popad
    pop gs
    pop fs
    pop es
    pop ds
    add esp, 4 ; 跳过error_code
    iretd

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
; 外设中断 
VECTOR 0x20, ZERO ; 时钟中断
VECTOR 0x21, ZERO ; 键盘中断
VECTOR 0x22, ZERO ; 级联口
VECTOR 0x23, ZERO ; 串口2中断
VECTOR 0x24, ZERO ; 串口1中断
VECTOR 0x25, ZERO ; 并口2中断
VECTOR 0x26, ZERO ; 软盘
VECTOR 0x27, ZERO ; 并口1中断
VECTOR 0x28, ZERO ; 实时时钟
VECTOR 0x29, ZERO ; 重定向
VECTOR 0x2a, ZERO
VECTOR 0x2b, ZERO 
VECTOR 0x2c, ZERO ; ps/2鼠标
VECTOR 0x2d, ZERO ; fpu浮点单元异常
VECTOR 0x2e, ZERO ; 硬盘
VECTOR 0x2f, ZERO
