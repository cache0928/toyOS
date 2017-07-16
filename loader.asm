%include "boot.inc"
SECTION Loader vstart=LOADER_BASE_ADDR
    mov byte [gs:0x00], 'B'
    mov byte [gs:0x01], 0xA4 ;A绿色背景，4红色前景

    mov byte [gs:0x02], 'o'
    mov byte [gs:0x03], 0xA4 

    mov byte [gs:0x04], 'o'
    mov byte [gs:0x05], 0xA4 

    mov byte [gs:0x06], 't'
    mov byte [gs:0x07], 0xA4 

    mov byte [gs:0x08], 'L'
    mov byte [gs:0x09], 0xA4 

    mov byte [gs:0x0A], 'o'
    mov byte [gs:0x0B], 0xA4 

    mov byte [gs:0x0C], 'a'
    mov byte [gs:0x0D], 0xA4 

    mov byte [gs:0x0E], 'd'
    mov byte [gs:0x0F], 0xA4 

    mov byte [gs:0x10], 'e'
    mov byte [gs:0x11], 0xA4

    mov byte [gs:0x12], 'r'
    mov byte [gs:0x13], 0xA4

    jmp $