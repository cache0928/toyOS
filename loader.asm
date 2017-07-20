%include "boot.inc"

SECTION LOADER vstart=LOADER_BASE_ADDR
LOADER_STACK_TOP equ LOADER_BASE_ADDR
jmp loader_start

;构建GDT以及内部的段描述符
GDT_BASE: dd 0x00000000
          dd 0x00000000
CODE_DESC: dd 0x0000FFFF
           dd DESC_CODE_HIGH4
DATA_STACK_DESC: dd 0x0000FFFF
                 dd DESC_DATA_HIGH4
VIDEO_DESC: dd 0x80000007 ; 显存段 limit=(0xbffff+1-0xb8000)/4k-1 = 7
            dd DESC_VIDEO_HIGH4

GDT_SIZE equ $-GDT_BASE
GDT_LIMIT equ GDT_SIZE-1
; 预留60个8字节空位
times 60 dq 0x0000
; 定义选择子
SELECTOR_CODE equ (0x0001<<3) + TI_GDT + RPL0
SELECTOR_DATA equ (0x0002<<3) + TI_GDT + RPL0
SELECTOR_VIDEO equ (0x0003<<3) + TI_GDT + RPL0

; 指向GDT的48位指针，要存入GDTR中
gdt_ptr dw GDT_LIMIT
        dd GDT_BASE

loadermsg db 'ready go to protection mode'

loader_start:
; 利用BIOS的0x13号中断打印字符串，显示进入保护模式
; AH 子功能号0x13
; BH 页码
; BL 字符属性
; CX 字符长度
; (DH, DL) = 坐标（行, 列）
; ES:BP 字符串地址
; AL 显示输出方式
    mov sp, LOADER_BASE_ADDR
    mov bp, loadermsg
    mov cx, 27
    mov ax, 0x1301
    mov bx, 0x001f
    mov dx, 0x1800
    int 0x10
; 准备进入保护模式
; 1. 打开A20
; 2. 加载GDT
; 3. 将CR0的PE位置1
; ---打开A20---
    in al, 0x92
    or al, 00000010B
    out 0x92, al

; ---加载GDT---
    lgdt [gdt_ptr]

; ---打开PE开关---
    mov eax, cr0
    or eax, 0x00000001
    mov cr0, eax

    jmp dword SELECTOR_CODE:p_mode_start ;刷新流水线

[bits 32]
p_mode_start:
    mov ax, SELECTOR_DATA
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov esp, LOADER_STACK_TOP
    mov ax, SELECTOR_VIDEO
    mov gs, ax
    ; 往显存段中写入数据
    mov byte [gs:160], 'P'
    mov byte [gs:162], ' '
    mov byte [gs:164], 'M'
    mov byte [gs:166], 'O'
    mov byte [gs:168], 'D'
    mov byte [gs:170], 'E'

    jmp $