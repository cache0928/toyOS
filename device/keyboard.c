#include "keyboard.h"
#include "print.h"
#include "io.h"
#include "global.h"
#include "interrupt.h"
#include "stdint.h"
#include "ioqueue.h"

// 键盘buffer寄存器的端口
#define KBD_BUF_PORT 0x60

/* 用转义字符定义部分控制字符 */
#define esc		'\033'
#define backspace	'\b'
#define tab		'\t'
#define enter		'\r'
#define delete		'\177'

// 没有对应ASCII的控制按键，全部定义为0占位
#define char_invisible	0
#define ctrl_l_char	char_invisible
#define ctrl_r_char	char_invisible
#define shift_l_char	char_invisible
#define shift_r_char	char_invisible
#define alt_l_char	char_invisible
#define alt_r_char	char_invisible
#define caps_lock_char	char_invisible

// 定义控制字符的通码
#define shift_l_make	0x2a
#define shift_r_make 	0x36 
#define alt_l_make   	0x38
#define alt_r_make   	0xe038
#define ctrl_l_make  	0x1d
#define ctrl_r_make  	0xe01d
#define caps_lock_make 	0x3a

// 记录相应控制键是否被按下，ext_scan_code代表上一次的编码是否为0xe0
static bool ctrl_status, shift_status, alt_status, caps_lock_status, ext_scan_code;

/* 以通码make_code为索引的二维数组 */
static char keymap[][2] = {
/* 扫描码   未与shift组合  与shift组合*/
/* 0x00 */	{0,	0},		
/* 0x01 */	{esc,	esc},		
/* 0x02 */	{'1',	'!'},		
/* 0x03 */	{'2',	'@'},		
/* 0x04 */	{'3',	'#'},		
/* 0x05 */	{'4',	'$'},		
/* 0x06 */	{'5',	'%'},		
/* 0x07 */	{'6',	'^'},		
/* 0x08 */	{'7',	'&'},		
/* 0x09 */	{'8',	'*'},		
/* 0x0A */	{'9',	'('},		
/* 0x0B */	{'0',	')'},		
/* 0x0C */	{'-',	'_'},		
/* 0x0D */	{'=',	'+'},		
/* 0x0E */	{backspace, backspace},	
/* 0x0F */	{tab,	tab},		
/* 0x10 */	{'q',	'Q'},		
/* 0x11 */	{'w',	'W'},		
/* 0x12 */	{'e',	'E'},		
/* 0x13 */	{'r',	'R'},		
/* 0x14 */	{'t',	'T'},		
/* 0x15 */	{'y',	'Y'},		
/* 0x16 */	{'u',	'U'},		
/* 0x17 */	{'i',	'I'},		
/* 0x18 */	{'o',	'O'},		
/* 0x19 */	{'p',	'P'},		
/* 0x1A */	{'[',	'{'},		
/* 0x1B */	{']',	'}'},		
/* 0x1C */	{enter,  enter},
/* 0x1D */	{ctrl_l_char, ctrl_l_char},
/* 0x1E */	{'a',	'A'},		
/* 0x1F */	{'s',	'S'},		
/* 0x20 */	{'d',	'D'},		
/* 0x21 */	{'f',	'F'},		
/* 0x22 */	{'g',	'G'},		
/* 0x23 */	{'h',	'H'},		
/* 0x24 */	{'j',	'J'},		
/* 0x25 */	{'k',	'K'},		
/* 0x26 */	{'l',	'L'},		
/* 0x27 */	{';',	':'},		
/* 0x28 */	{'\'',	'"'},		
/* 0x29 */	{'`',	'~'},		
/* 0x2A */	{shift_l_char, shift_l_char},	
/* 0x2B */	{'\\',	'|'},		
/* 0x2C */	{'z',	'Z'},		
/* 0x2D */	{'x',	'X'},		
/* 0x2E */	{'c',	'C'},		
/* 0x2F */	{'v',	'V'},		
/* 0x30 */	{'b',	'B'},		
/* 0x31 */	{'n',	'N'},		
/* 0x32 */	{'m',	'M'},		
/* 0x33 */	{',',	'<'},		
/* 0x34 */	{'.',	'>'},		
/* 0x35 */	{'/',	'?'},
/* 0x36	*/	{shift_r_char, shift_r_char},	
/* 0x37 */	{'*',	'*'},    	
/* 0x38 */	{alt_l_char, alt_l_char},
/* 0x39 */	{' ',	' '},		
/* 0x3A */	{caps_lock_char, caps_lock_char}
/*其它按键暂不处理*/
};

static struct ioqueue kbd_buf;

static void intr_keyboard_handler() {
    // 上一次中断发生时，以下3个键的状态
    bool ctrl_down_last = ctrl_status;
    bool shift_down_last = shift_status;
    bool caps_lock_last = caps_lock_status;

    bool break_code;
    uint16_t scan_code = inb(KBD_BUF_PORT);
    // 如果是e0开头，则表示多个扫描码组合成一个按键，立马return等待下一部分的扫描码
    if (scan_code == 0xe0) {
        ext_scan_code = true;
        return;
    }
    // 如果上一次是e0开头，则将扫描码合并
    if (ext_scan_code) {
        scan_code = (0xe000 | scan_code);
        ext_scan_code = false;
    }
    // 最高位是1表示是断码而不是通码，表示按键松开
    break_code = ((scan_code & 0x0080) != 0);
    if (break_code) {
        // 如果是断码
        // 获取对应断码的通码来判断是哪个按键
        uint16_t make_code = (scan_code & 0xff7f);
        if (make_code == ctrl_l_make || make_code == ctrl_r_make) {
            ctrl_status = false;
        } else if (make_code == shift_l_make || make_code == shift_r_make) {
            shift_status = false;
        } else if (make_code == alt_l_make || make_code == alt_r_make) {
            alt_status = false;
        }
        // cap_lock不是弹起就算关闭，所以单独处理
        return;
    } else if ((scan_code > 0x00 && scan_code < 0x3b) || (scan_code == alt_r_make) || (scan_code == ctrl_r_make)) {
        bool shift = false;
        if ((scan_code < 0x0e) || (scan_code == 0x29) || (scan_code == 0x1a) || (scan_code == 0x1b) || (scan_code == 0x2b) || (scan_code == 0x27) || (scan_code == 0x28) || (scan_code == 0x33) || (scan_code == 0x34) || (scan_code == 0x35)) {
            // 代表有2个字母的按键，需要shift状态来切换
            if (shift_down_last) {
                shift = true;
            }
        } else {
            // 字母键
            if (shift_down_last && caps_lock_last) {
                shift = false;
            } else if (shift_down_last || caps_lock_last) {
                shift = true;
            } else {
                shift = false;
            }
        }
        // 如果有高字节e0.则将其清空
        uint8_t index = (scan_code & 0x00ff);
        char cur_char = keymap[index][shift];
        // 只打印拥有对应ASCII的按键
        if (cur_char) {
            if (!ioq_full(&kbd_buf)) {
                put_char(cur_char);
                ioq_putchar(&kbd_buf, cur_char);
            }
            return;
        }
        // 如果本次按下的是控制键
        if (scan_code == ctrl_l_make || scan_code == ctrl_r_make) {
            ctrl_status = true;
        } else if (scan_code == shift_l_make || scan_code == shift_r_make) {
            shift_status = true;
        } else if (scan_code == alt_l_make || scan_code == alt_r_make) {
            alt_status = true;
        } else if (scan_code == caps_lock_make) {
            // 按下caps_lock相当于切换状态
            caps_lock_status = !caps_lock_status;
        }
    } else {
        put_str("unknown key\n");
    }
}

void keyboard_init() {
    put_str("keyboard init start\n");
    ioqueue_init(&kbd_buf);
    register_handler(0x21, intr_keyboard_handler);
    put_str("keyboard init done\n");
}