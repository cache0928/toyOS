#include "interrupt.h"
#include "global.h"
#include "stdint.h"
#include "print.h"
#include "io.h"

#define IDT_DESC_CNT 0x21 // 中断描述符表中描述符的个数

#define EFLAGS_IF 0x00000200 // eflags寄存器中if位为1
// 获取当前eflags寄存器值的宏
#define GET_EFLAGS(EFLAG_VAR) asm volatile ("pushf; pop %0" : "=g" (EFLAG_VAR))

// PIC 8259A的端口定义
#define PIC_M_CTRL 0x20
#define PIC_M_DATA 0x21
#define PIC_S_CTRL 0xA0
#define PIC_S_DATA 0xA1
// 初始化8259A
static void pic_init() {
    /*
    ICW1 和 OCW2、OCW3 是用偶地址端口 0x20（主片）或 0xA0（从片）写入。
    ICW2～ICW4 和 OCW1 是用奇地址端口 0x21（主片）或 0xA1（从片）写入。
    */
    // 主片
    outb(PIC_M_CTRL, 0x11); // ICW1: 边沿触发，级联，需要ICW4
    outb(PIC_M_DATA, 0x20); // ICW2: 初始中断向量号为0x20，即IR[0-7]为0x20-0x27
    outb(PIC_M_DATA, 0x04); // ICW3: IR2口接从片
    outb(PIC_M_DATA, 0x01); // ICW4: 8086模式，正常手动发送EOI
    // 从片
    outb(PIC_S_CTRL, 0x11); // ICW1: 边沿触发，级联，需要ICW4
    outb(PIC_S_DATA, 0x28); // ICW2: 起始中断向量号为0x28, 即IR[8-15]为0x28-0x2f
    outb(PIC_S_DATA, 0x02); // ICW3: 设置从片连接到主片的IR2引脚
    outb(PIC_S_DATA, 0x01); // ICW4: 8086模式，正常手动发送EOI

    // 发送OCW1，设置屏蔽哪些外设的中断信号，目前只接受时钟中断
    outb(PIC_M_DATA, 0xfe);
    outb(PIC_S_DATA, 0xff); 

    put_str("   pic_init done\n");
}

// 中断门描述符结构体
struct gate_desc {
    uint16_t func_offset_low_word;
    uint16_t selector;
    uint8_t dcount;
    uint8_t attribute;
    uint16_t func_offset_high_word;
};

char * intr_name[IDT_DESC_CNT]; // 中断的名称数组
extern intr_handler intr_entry_table[IDT_DESC_CNT]; // 在kernel.s中的中断处理函数入口数组
intr_handler idt_table[IDT_DESC_CNT]; // 实际的中断处理函数地址数组，通过上面的入口函数在汇编中call来进入

// 构造中断门描述符的函数
static void make_idt_desc(struct gate_desc *p_gdesc, uint8_t attr, intr_handler function);
// 中断描述符表IDT，本质就是中断门描述符的数组
static struct gate_desc idt[IDT_DESC_CNT];

static void make_idt_desc(struct gate_desc *p_gdesc, uint8_t attr, intr_handler function) {
    p_gdesc -> func_offset_low_word = (uint32_t)function & 0x0000FFFF;
    p_gdesc -> selector = SELECTOR_K_CODE;
    p_gdesc -> dcount = 0;
    p_gdesc -> attribute = attr;
    p_gdesc -> func_offset_high_word = ((uint32_t)function & 0xFFFF0000) >> 16;
}

// 构造中断描述符表IDT
static void idt_desc_init() {
    int i;
    for (i = 0; i < IDT_DESC_CNT; i++) {
        make_idt_desc(&idt[i], IDT_DESC_ATTR_DPL0, intr_entry_table[i]);
    }
    put_str("   idt_desc_init done\n");
}

// 通用的中断处理函数，没有指定特定中断处理函数的时候就用这个
static void general_intr_handler(uint8_t vec_nr) {
    static int index = 0;
    index++;
    if (vec_nr == 0x27 || vec_nr == 0x2f) {
        // IRQ7和IRQ15产生的是伪中断(一些电气信息异常之类的)，无需处理，直接跳过
        return;
    }
    put_str("int vector : 0x");
    put_int(vec_nr);
    put_char(' ');
    put_int(index);
    put_char('\n');
}

// 填充中断名称数组，以及为每个中断设置默认的中断处理函数
static void exception_init(void) {
    int i;
    for (i = 0; i < IDT_DESC_CNT; i++) {
        /* idt_table数组中的函数是在进入中断后根据中断向量号调用的, call [idt_table + %1*4] */
        idt_table[i] = general_intr_handler;
        intr_name[i] = "unknown";
    }
    intr_name[0] = "#DE Divide Error";
    intr_name[1] = "#DB Debug Exception";
    intr_name[2] = "NMI Interrupt";
    intr_name[3] = "#BP Breakpoint Exception";
    intr_name[4] = "#OF Overflow Exception";
    intr_name[5] = "#BR BOUND Range Exceeded Exception";
    intr_name[6] = "#UD Invalid Opcode Exception";
    intr_name[7] = "#NM Device Not Available Exception";
    intr_name[8] = "#DF Double Fault Exception";
    intr_name[9] = "Coprocessor Segment Overrun";
    intr_name[10] = "#TS Invalid TSS Exception";
    intr_name[11] = "#NP Segment Not Present";
    intr_name[12] = "#SS Stack Fault Exception";
    intr_name[13] = "#GP General Protection Exception";
    intr_name[14] = "#PF Page-Fault Exception";
    // intr_name[15] 第15项是intel保留项，未使用
    intr_name[16] = "#MF x87 FPU Floating-Point Error";
    intr_name[17] = "#AC Alignment Check Exception";
    intr_name[18] = "#MC Machine-Check Exception";
    intr_name[19] = "#XF SIMD Floating-Point Exception";
}

// 完成所有中断有关的初始化
void idt_init() {
    put_str("idt_init start\n");
    idt_desc_init();
    exception_init();
    pic_init();

    // 设置IDTR
    // 0-15位段界限， 16-47位IDT的地址
    uint64_t idt_operand = (sizeof(idt) - 1) | ((uint64_t)((uint32_t)idt << 16));
    asm volatile ("lidt %0" : : "m"(idt_operand));

    put_str("idt_init done\n");
}

// 打开中断，并返回开启中断前的状态
enum intr_status intr_enable() {
    enum intr_status old_status;
    if (intr_get_status() == INTR_ON) {
        old_status = INTR_ON;
    } else {
        old_status = INTR_OFF;
        asm volatile ("sti"); // 开中断
    }
    return old_status;
}

// 关闭中断，并返回关闭中断前的状态
enum intr_status intr_disable() {
    enum intr_status old_status;
    if (intr_get_status() == INTR_ON) {
        old_status = INTR_ON;
        asm volatile ("cli" : : : "memory"); // 关中断
    } else {
        old_status = INTR_OFF;
    }
    return old_status;
}

// 设置中断状态
enum intr_status intr_set_status(enum intr_status status) {
    return status & INTR_ON ? intr_enable() : intr_disable();
}

// 获取当前的中断状态
enum intr_status intr_get_status() {
    uint32_t eflags = 0;
    GET_EFLAGS(eflags);
    return (EFLAGS_IF & eflags) ? INTR_ON : INTR_OFF;
}