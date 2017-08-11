[bits 32]
section .text
global switch_to
switch_to:
    ; 在push之前是switch的返回地址
    push esi
    push edi
    push ebx
    push ebp
    mov eax, [esp + 20] ; 栈中的cur参数
    mov [eax], esp ; cur指向当前线程的pcb，而pcb第一项就是self_kstack，正好用来保存自己的栈顶指针

    ; 上面是备份当前线程cur的环境，下面是恢复next的线程环境
    mov eax, [esp + 24] ; 栈中的next参数
    mov esp, [eax] ; 通过next的pcb中的self_kstack恢复栈顶指针

    pop ebp
    pop ebx
    pop edi
    pop esi
    ; 返回
    ; 对于第一次被调度的线程，返回地址是pcb中被事先安排好的kernel_thread的地址
    ; 而对于不是第一次被调度的线程，返回地址是调用switch_to的地方
    ret
