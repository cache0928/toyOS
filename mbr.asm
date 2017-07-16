%include "boot.inc"
SECTION MBR vstart=0x7c00
    mov ax, cs
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    ; 栈指针定义到7c00，7c00以上的内容都有作用，避免被栈覆盖
    mov sp, 0x7c00
    ; 显存在实模式下的映射起始地址
    mov ax, 0xb800
    mov gs, ax

    ; 清屏，并设置行列大小为80*25
    mov ax, 0x600
    mov bx, 0x700
    mov cx, 0
    mov dx, 0x184f ; 0x18 = 24, 0x4f = 79
    int 0x10

    mov byte [gs:0x00], 'H'
    mov byte [gs:0x01], 0xA4 ;A绿色背景，4红色前景

    mov byte [gs:0x02], 'e'
    mov byte [gs:0x03], 0xA4 

    mov byte [gs:0x04], 'l'
    mov byte [gs:0x05], 0xA4 

    mov byte [gs:0x06], 'l'
    mov byte [gs:0x07], 0xA4 

    mov byte [gs:0x08], 'o'
    mov byte [gs:0x09], 0xA4 

    mov byte [gs:0x0A], ' '
    mov byte [gs:0x0B], 0xA4 

    mov byte [gs:0x0C], 'M'
    mov byte [gs:0x0D], 0xA4 

    mov byte [gs:0x0E], 'B'
    mov byte [gs:0x0F], 0xA4 

    mov byte [gs:0x10], 'R'
    mov byte [gs:0x11], 0xA4

    mov eax, LOADER_START_SECTOR ; 起始扇区的LBA地址，这里为第2号扇区, 即Boot Loader在磁盘中保存的位置
    mov bx, LOADER_BASE_ADDR ; 扇区中的内容写入到内存中时的起始地址, 这里即Boot Loader的起始地址
    mov cx, 1 ; 待读入的扇区数
    call rd_disk_m_16

    jmp LOADER_BASE_ADDR ; MBR转交控制权到Boot Loader手中 

; 函数，读取硬盘中的N个扇区
; eax=LBA扇区号
; bx=将数据写入的内存地址
; cx=读入的扇区数
rd_disk_m_16:
    mov esi, eax ; 备份eax
    mov di, cx ; 备份cx

    ; 1.设置要读写的扇区数，写入sector count端口
    mov dx, 0x1f2
    mov al, cl
    out dx, al

    mov eax, esi
    ; 2.将LBA扇区号（28位）存入到端口号0x1f3 ~ 0x1f6
    ; 7 ～ 0位写入端口0x1f3
    mov dx, 0x1f3
    out dx, al
    ; 15 ～ 8位写入端口0x1f4
    mov cl, 8
    shr eax, cl
    mov dx, 0x1f4
    out dx, al
    ; 23 ～ 16位写入端口0x1f5
    shr eax, cl
    mov dx, 0x1f5
    out dx, al
    ; LBA扇区号第27 ～ 24位写入到device端口低4位，高4位设置成1110，表示lba模式下的主盘
    shr eax, cl
    and al, 0x0f
    or al, 0xe0
    mov dx, 0x1f6
    out dx, al
    ; 3.向command端口写入读命令，0x20
    mov dx, 0x1f7
    mov al, 0x20
    out dx, al
    ; 4.检测硬盘状态， 即检测status端口（即写状态时的command端口）
.not_ready:
    nop
    in al, dx
    ; 第4位为1表示数据已经准备好，第7位为1表示硬盘忙
    and al, 0x88
    cmp al, 0x08
    jnz .not_ready
    ; 5.从data端口读取数据
    mov ax, di
    mov dx, 256
    ; 因为data端口为16位，因此一个扇区读取要256次，计算总共要读取的次数
    mul dx
    mov cx, ax

    mov dx, 0x1f0
.go_on_read:
    in ax, dx
    mov [bx], ax
    add bx, 2
    loop .go_on_read
    ret

    ; 填充至512字节，MBR要求最后两字节为0x55, 0xaa
    times 510-($-$$) db 0
    db 0x55, 0xaa