TI_GDT equ 0
RPL0 equ 0
SELECTOR_VIDEO equ (0x0003<<3)+TI_GDT+RPL0

[bits 32]
section .data
put_int_buffer dq 0 ; 定义8字节缓冲区用于数字到字符串的转化
; put_char 将栈中的1个字符写入光标所在处
; put_str 通过put_char打印以0字符结尾的字符串
; put_int 通过put_char打印16进制数字，无0x前缀
section .text
global put_str
put_str:
    push ebx
    push ecx
    xor ecx, ecx
    mov ebx, [esp+12] ; 待打印字符串的地址
.goon:
    mov cl, [ebx]
    cmp cl, 0 ; 如果到了0，则表示字符串结束
    jz .str_over
    push ecx ; 给put_char传参
    call put_char
    add esp, 4 ; 回收参数的栈空间
    inc ebx ; 下一个字符
    jmp .goon
.str_over:
    pop ecx
    pop ebx
    ret

global put_char
put_char:
    pushad ; 将所有32位寄存器入栈
    mov ax, SELECTOR_VIDEO
    mov gs, ax

    ; 获取当前光标位置
    ; 获取高8位
    mov dx, 0x03d4 ; 索引寄存器
    mov al, 0x0e ; 用于光标位置高8位寄存器的索引值
    out dx, al
    mov dx, 0x03d5 ; 数据寄存器
    in al, dx
    mov ah, al
    ; 获取低8位
    mov dx, 0x03d4 ; 索引寄存器
    mov al, 0x0f ; 用于光标位置高8位寄存器的索引值
    out dx, al
    mov dx, 0x03d5 ; 数据寄存器
    in al, dx
    ; 保存光标位置
    mov bx, ax

    ; 取得栈中的待打印字符
    mov ecx, [esp + 36] ; pushd压入8*4=32字节，再加上调用时候的返回地址4字节

    cmp cl, 0xd ; 是回车
    jz .is_carriage_return
    cmp cl, 0xa ; 是换行
    jz .is_line_feed

    cmp cl, 0x8 ; 是退格
    jz .is_backspace
    jmp .put_other

.is_backspace:
    dec bx ; 光标往前移一个索引
    shl bx, 1 ; 获取当前索引的字节号
    ; 用空格将该索引表示的字符替换掉
    mov byte [gs:bx], 0x20
    inc bx
    mov byte [gs:bx], 0x07
    shr bx, 1 ; 将bx从字节号换算回索引值
    jmp .set_cursor

.put_other:
    shl bx, 1
    mov [gs:bx], cl ; 待打印字符存在ecx中
    inc bx
    mov byte [gs:bx], 0x07 ; 字符样式
    shr bx, 1 ; 恢复到老的索引值
    inc bx ; 光标索引值+1
    cmp bx, 2000 ; 是否已经满屏
    jl .set_cursor

.is_line_feed: ; 是换行
.is_carriage_return: ; 是回车
    ; 将bx置于当前行行首的索引值， 当前索引-（当前索引/80的余数）
    xor dx, dx
    mov ax, bx
    mov si, 80
    div si ; 余数在dx中
    sub bx, dx
.is_carriage_return_end:
    add bx, 80 ; 索引切换到下一行行首
    cmp bx, 2000
.is_line_feed_end:
    jl .set_cursor

; 屏幕范围0-25行，满屏的时候才用第一行滚出屏幕，最后行用空格填充
.roll_screen:
    cld
    mov ecx, 960
    mov esi, 0xc00b80a0 ; 第1行首
    mov edi, 0xc00b8000 ; 第0行首
    rep movsd
    ; 用空格填充最后一行
    mov ebx, 3840 ; 最后行首的字节号
    mov ecx, 80
.cls:
    mov word [gs:ebx], 0x0720 ; 黑底白字的空格
    add ebx, 2
    loop .cls
    mov bx, 1920 ; 光标索引重置为最后行首

.set_cursor:
    ; 将光标设置为bx的值
    ; 高8位
    mov dx, 0x03d4 ; 索引寄存器
    mov al, 0x0e
    out dx, al
    mov dx, 0x03d5 ; 数据寄存器
    mov al, bh
    out dx, al
    ; 低8位
    mov dx, 0x03d4 ; 索引寄存器
    mov al, 0x0f
    out dx, al
    mov dx, 0x03d5 ; 数据寄存器
    mov al, bl
    out dx, al
.put_char_done:
    popad
    ret

global put_int
put_int:
    pushad
    mov ebp, esp
    mov eax, [ebp+4*9] ; 传入的整数
    mov edx, eax
    mov edi, 7 ; 指定put_int_buffer的起始偏移量，将转化后的字符串以此为起点依次存入buffer中
    mov ecx, 8 ; int对应4byte，也就是8个16进制的数字
    mov ebx, put_int_buffer
    
    ; 将32位数字按低字节->高字节的顺序逐个处理，结果存入buffer中
.16based_4bits:
    and edx, 0x0000000F
    cmp edx, 9
    jg .is_A2F
    add edx, '0'
    jmp .store
.is_A2F:
    sub edx, 10
    add edx, 'A'
.store:
    mov [ebx+edi], dl ; dl中有当前处理完的字符
    dec edi
    shr eax, 4
    mov edx, eax
    loop .16based_4bits

.ready_to_print:
    inc edi ; 字符依次存入缓冲区完成后，edi为-1.所以要先+1
.skip_prefix_0:
    cmp edi, 8 ; 如果已经打印到最后个字符还是0， 说明整个整数就是0
    je .full0
.go_on_skip:
    mov cl, [put_int_buffer+edi]
    inc edi
    cmp cl, '0'
    je .skip_prefix_0
    dec edi ; edi在上面的inc之后已经指向了下一个字符
    jmp .put_each_num
.full0:
    mov cl, '0'
.put_each_num:
    push ecx
    call put_char
    add esp, 4
    inc edi
    mov cl, [put_int_buffer+edi]
    cmp edi, 8
    jl .put_each_num
    popad
    ret
