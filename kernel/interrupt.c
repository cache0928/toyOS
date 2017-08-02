#include "interrupt.h"
#include "global.h"
#include "stdint.h"
#include "print.h"
#include "io.h"

#define IDT_DESC_CNT 0x21 // 中断描述符表中描述符的个数

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

extern intr_handler intr_entry_table[IDT_DESC_CNT]; // 在kernel.s中的中断处理函数入口数组
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

// 完成所有中断有关的初始化
void idt_init() {
    put_str("idt_init start\n");
    idt_desc_init();
    pic_init();

    // 设置IDTR
    // 0-15位段界限， 16-47位IDT的地址
    uint64_t idt_operand = (sizeof(idt) - 1) | ((uint64_t)((uint32_t)idt << 16));
    asm volatile ("lidt %0" : : "m"(idt_operand));

    put_str("idt_init done\n");
}