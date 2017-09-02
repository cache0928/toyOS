#include "shell.h"
#include "stdio.h"
#include "stdint.h"
#include "syscall.h"
#include "file.h"
#include "string.h"
#include "global.h"
#include "buildin_cmd.h"

#define cmd_len 128

static char cmd_line[cmd_len] = {0};
char final_path[MAX_PATH_LEN] = {0};

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

#define MAX_ARG_NR 16

// 解析输入，将cmd_str以token为分隔符拆分，将每个部分的字符串存入argv，参数过多返回-1
static int32_t cmd_parse(char *cmd_str, char **argv, char token) {
    int32_t arg_idx = 0;
    while (arg_idx < MAX_ARG_NR) {
        argv[arg_idx] = NULL;
        arg_idx++;
    }
    char *next = cmd_str;
    int32_t argc = 0;
    while (*next != 0) {
        while (*next == token) {
            next++;
        }
        if (*next == 0) {
            break;
        }
        if (argc >= MAX_ARG_NR) {
            return -1;
        }
        argv[argc] = next;
        while (*next && *next != token) {
            next++;
        }
        if (*next) {
            *next = 0;
            next++;
        }
        argc++;
    }
    return argc;
}

char *argv[MAX_ARG_NR];
int32_t argc = -1;

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
        argc = -1;
        argc = cmd_parse(cmd_line, argv, ' ');
        if (argc == -1) {
            printf("num of arguments exceed %d\n", MAX_ARG_NR);
            continue;
        }
        if (!strcmp("ls", argv[0])) {
            buildin_ls(argc, argv);
        } else if (!strcmp("cd", argv[0])) {
            if (buildin_cd(argc, argv) != NULL) {
                memset(cwd_cache, 0, MAX_PATH_LEN);
                strcpy(cwd_cache, final_path);
            }
        } else if (!strcmp("pwd", argv[0])) {
            buildin_pwd(argc, argv);
        } else if (!strcmp("ps", argv[0])) {
            buildin_ps(argc, argv);
        } else if (!strcmp("clear", argv[0])) {
            buildin_clear(argc, argv);
        } else if (!strcmp("mkdir", argv[0])){
            buildin_mkdir(argc, argv);
        } else if (!strcmp("rmdir", argv[0])){
            buildin_rmdir(argc, argv);
        } else if (!strcmp("rm", argv[0])) {
            buildin_rm(argc, argv);
        } else {
            printf("external command\n");
        }
    }
    // panic("my_shell: should not be here");
}