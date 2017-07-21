%include "boot.inc"

SECTION LOADER vstart=LOADER_BASE_ADDR
LOADER_STACK_TOP equ LOADER_BASE_ADDR

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

; 用于保存内存容量大小，此处地址为0x900+0x200(512)=0xb00
total_mem_bytes dd 0

; 指向GDT的48位指针，要存入GDTR中
gdt_ptr dw GDT_LIMIT
        dd GDT_BASE

; 手动对齐，total_mem_bytes4 + gdt_ptr6 + ards_buff244 + ards_nr2 = (0x100)256字节
; 为了使loader_start在文件中的偏移地址为0x200+0x100=0x300
ards_buf times 244 db 0
ards_nr dw 0


; 定义段选择子
SELECTOR_CODE equ (0x0001<<3) + TI_GDT + RPL0
SELECTOR_DATA equ (0x0002<<3) + TI_GDT + RPL0
SELECTOR_VIDEO equ (0x0003<<3) + TI_GDT + RPL0

loader_start:
; ---0xE820号方式获取内存信息, 内存信息使用ards结构描述, edx=0x534d4150('SMAP')---
    xor ebx, ebx ; 第一次ebx要请0，之后的循环中会自动改变为下一个待操作的ARDS的编号
    mov edx, 0x534d4150
    mov di, ards_buf ; 记录ARDS的缓冲区
.e820_mem_get_loop:
    mov eax, 0x0000e820 ; 每次中断以后，eax会变成'SMAP的ASCII编码0x534d4150, 所以每次循环都要重新赋值
    mov ecx, 20 ; ARDS的结构大小为20字节
    int 0x15
    jc .e820_failed_so_try_e801 ; 如果cf位为1则有错误发生，尝试下一种获取内存信息的方法
    add di, cx ; 移动ARDS缓冲区指针指向下一个待记录的位置
    inc word [ards_nr] ; ARDS的数量+1
    cmp ebx, 0 ; 如果ebx为0且cf不为1，则说明ards已经全部获取完成
    jnz .e820_mem_get_loop
; 遍历ards缓冲区中的ards
; 因为是32位系统，所以找出base_add_low + length_low最大值即内存的容量
    mov cx, [ards_nr]
    mov ebx, ards_buf
    xor edx, edx
.find_max_mem_area: ; 无需判断type，最大的内存一定是可被操作系统使用的
    mov eax, [ebx] ; base_add_low
    add eax, [ebx + 8] ; length
    add ebx, 20
    cmp edx, eax
    jge .next_ards
    mov edx, eax
.next_ards:
    loop .find_max_mem_area
    jmp .mem_get_ok
; ---0xe801号方式获取内存大小，最大支持4g---
.e820_failed_so_try_e801:
    mov ax, 0xe801
    int 0x15
    jc .e801_failed_so_try_88
; 先算出低15MB大小：ax * 1kb + 1mb(位于15mb-16mb之间，用于isa设备)
    mov cx, 0x400 ; 1kb
    mul cx
    shl edx, 16
    and eax, 0x0000ffff
    or edx, eax
    add edx, 0x100000 ; 1mb
    mov esi, edx ; 暂存低15mb的容量
; 再计算16mb直到4gb的内存大小: bx * 64kb
    xor eax, eax
    mov ax, bx
    mov ecx, 0x10000 ; 64kb
    mul ecx
    ; 最大也就4g，32位寄存器足够了
    add esi, eax
    mov edx, esi
    jmp .mem_get_ok
; --- 0x88号方式获取内存大小，最高只支持64mb---
.e801_failed_so_try_88:
    mov ah, 0x88
    int 0x15 ; 中断后ax中存入的是kb为单位的内存容量
    jc .error_hlt
    and eax, 0x0000ffff
    mov cx, 0x400
    mul cx
    shl edx, 16
    or edx, eax
    add edx, 0x100000 ; 0x88号只返回1mb以上的内存，所以总容量要加上这1mb
.mem_get_ok:
    mov [total_mem_bytes], edx
.error_hlt:
    jmp $







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