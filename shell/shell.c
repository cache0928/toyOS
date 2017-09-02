#include "shell.h"
#include "stdio.h"
#include "stdint.h"
#include "syscall.h"
#include "file.h"
#include "string.h"

#define cmd_len 128
#define MAX_ARG_NR 16

static char cmd_line[cmd_len] = {0};

char cwd_cache[64];

void print_prompt() {
    printf("<toyOS:%s cache> $ ", cwd_cache);
}

// 从键盘缓冲区读取最多count个字符到buf
static void readline(char *buf, int32_t count) {
    char* pos = buf;
    while (read(stdin_no, pos, 1) != -1 && (pos - buf) < count) {
        switch (*pos) {
            case '\n':
            case '\r':
                *pos = 0;
                putchar('\n');
                return;
            case '\b':
                if (buf[0] != '\b') {
                    --pos;
                    putchar('\b');
                }
                break;
            default:
                putchar(*pos);
                pos++;
        }
    }
    printf("readline: can't find enter_key in the cmd_line, max num of char is 128\n");
}

void my_shell() {
    cwd_cache[0] = '/';
    while (1) {
        print_prompt();
        memset(cmd_line, 0, cmd_len);
        readline(cmd_line, cmd_len);
        if (cmd_line[0] == 0) {
            // 只输入了一个回车
            continue;
        }
    }
    // panic("my_shell: should not be here");
}