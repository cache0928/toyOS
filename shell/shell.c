#include "shell.h"
#include "stdio.h"
#include "stdint.h"
#include "syscall.h"
#include "file.h"
#include "string.h"
#include "global.h"
#include "buildin_cmd.h"
#include "pipe.h"

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

static void execute_cmd(char **argv, uint32_t argc) {
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
    } else if (!strcmp("help", argv[0])) {
        buildin_help(argc, argv);
    } else {
        int32_t pid = fork();
        if (pid) {
            // 父进程
            int32_t status;
            int32_t child_pid = wait(&status);
            printf("child_pid %d, it's status: %d\n", child_pid, status);
        } else {
            // 子进程
            make_clear_abs_path(argv[0], final_path);
            argv[0] = final_path;
            struct stat file_stat;
            memset(&file_stat, 0, sizeof(struct stat));
            if (stat(argv[0], &file_stat) == -1) {
                // 文件不存在
                printf("my_shell: cannot access %s: No such file or directory\n", argv[0]);
            } else {
                execv(argv[0], argv);
            }
        }
    }
} 

void my_shell() {
    cwd_cache[0] = '/';
    while (1) {
        print_prompt();
        memset(final_path, 0, MAX_PATH_LEN);
        memset(cmd_line, 0, cmd_len);
        readline(cmd_line, cmd_len);
        if (cmd_line[0] == 0) {
            // 只输入了一个回车
            continue;
        }
        // 解析是否有管道符
        char *pipe_symbol = strchr(cmd_line, '|');
        if (pipe_symbol) {
            // 管道可能多级，如cmd1 | cmd2 | cmd3 | cmd4
            // 此时需要在执行cmd1时把标准输出重定向到管道，开始执行cmd2时将标准输入重定向到管道，从而获取之前的输出
            // 等到执行cmd4时，需要把标准输出重置，才能在屏幕上输出内容

            // 生成管道
            // fd[0]管道出口，fd[1]管道入口
            int32_t fd[2] = {-1};
            pipe(fd);
            // 重定向标准输出到管道入口，准备执行第一个命令
            fd_redirect(stdout_no, fd[1]);

            // 获取第一个命令并执行
            char* each_cmd = cmd_line;
            pipe_symbol = strchr(each_cmd, '|');
            *pipe_symbol = 0;
            argc = -1;
            argc = cmd_parse(each_cmd, argv, ' ');
            execute_cmd(argv, argc);

            // 跨过管道符，指向管道符之后的起始位置
            each_cmd = pipe_symbol + 1;

            // 将标准输入重定向到管道，接收之前命令的输出
            fd_redirect(stdin_no, fd[0]);
            // 不断循环获取命令并执行，直到剩下的命令中不再有管道符
            while ((pipe_symbol = strchr(each_cmd, '|'))) { 
                *pipe_symbol = 0;
                argc = -1;
                argc = cmd_parse(each_cmd, argv, ' ');
                execute_cmd(argv, argc);
                each_cmd = pipe_symbol + 1;
            }

            // 剩下的命令中不再有管道符，说明到了最后一个命令，需要重置标准输出
            fd_redirect(stdout_no, stdout_no);

            // 执行最后一个cmd
            argc = -1;
            argc = cmd_parse(each_cmd, argv, ' ');
            execute_cmd(argv, argc);

            // 重置标准输入
            fd_redirect(stdin_no, stdin_no);

            // 关闭管道
            close(fd[0]);
            close(fd[1]);
        } else {
            argc = -1;
            argc = cmd_parse(cmd_line, argv, ' ');
            if (argc == -1) {
                printf("num of arguments exceed %d\n", MAX_ARG_NR);
                continue;
            }
            execute_cmd(argv, argc);
        }
        int32_t arg_idx = 0;
        while (arg_idx < MAX_ARG_NR) {
            argv[arg_idx] = NULL;
            arg_idx++;
        }
    }
    // panic("my_shell: should not be here");
}