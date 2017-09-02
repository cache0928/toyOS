#include "buildin_cmd.h"
#include "syscall.h"
#include "stdio.h"
#include "string.h"
#include "fs.h"
#include "global.h"
#include "dir.h"
#include "shell.h"

// 将路径old_abs_path中的..和.转换为实际路径后存入new_abs_path
static void wash_path(char* old_abs_path, char* new_abs_path) {
    char name[MAX_FILE_NAME_LEN] = {0};    
    char* sub_path = old_abs_path;
    sub_path = path_parse(sub_path, name);
    if (name[0] == 0) { 
        new_abs_path[0] = '/';
        new_abs_path[1] = 0;
        return;
    }
    new_abs_path[0] = 0;
    strcat(new_abs_path, "/");
    while (name[0]) {
        if (!strcmp("..", name)) {
            char* slash_ptr =  strrchr(new_abs_path, '/');
            if (slash_ptr != new_abs_path) {	
                // 如new_abs_path为“/a/b”,".."之后则变为“/a”
                *slash_ptr = 0;
            } else {
                // 从右到左第一个/如果就在开头，说明就是/目录，/目录的父目录是自己
                *(slash_ptr + 1) = 0;
            }
        } else if (strcmp(".", name)) {	  
            // 如果路径不是‘.’,就将name拼接到new_abs_path
            if (strcmp(new_abs_path, "/")) {	  
                // 如果new_abs_path不是"/",就拼接一个"/",此处的判断是为了避免路径开头变成这样"//"
                strcat(new_abs_path, "/");
            }
            strcat(new_abs_path, name);
        }  // 若name为当前目录".",无须处理new_abs_path

        // 继续遍历下一层路径
        memset(name, 0, MAX_FILE_NAME_LEN);
        if (sub_path) {
            sub_path = path_parse(sub_path, name);
        }
    }
}

// 将path处理成不含..和.的绝对路径,存储在final_path
void make_clear_abs_path(char* path, char* final_path) {
    char abs_path[MAX_PATH_LEN] = {0};
    if (path[0] != '/') {     
        // 若输入的不是绝对路径,就拼接成绝对路径
        memset(abs_path, 0, MAX_PATH_LEN);
        if (getcwd(abs_path, MAX_PATH_LEN) != NULL) {
            if (!((abs_path[0] == '/') && (abs_path[1] == 0))) {	     
                // 若不是根目录则要拼接一个/
                strcat(abs_path, "/");
            }
        }
    }
    strcat(abs_path, path);
    wash_path(abs_path, final_path);
}


// 内建命令 
// 内建命令的实现规则：
// （1）内部命令都以前缀“buildin_”+“命令名”的形式命名，如 cd 命令的函数是 buildin_cd。 
// （2）形参均是 argc 和 argv，argc 是参数数组 argv 中参数的个数。 
// （3）函数实现是调用同功能的系统调用实现的，如函数 buildin_cd 是调用系统调用 chdir 完成的。 
// （4）在进行系统调用前调用函数 make_clear_abs_path 把路径转换为绝对路径。


// 以下代码直接拷贝自《操作系统真象还原》

/* pwd命令的内建函数 */
void buildin_pwd(uint32_t argc, char **argv) {
    if (argc != 1) {
        printf("pwd: no argument support!\n");
        return;
    } else {
        if (NULL != getcwd(final_path, MAX_PATH_LEN)) {
            printf("%s\n", final_path); 
        } else {
            printf("pwd: get current work directory failed.\n");
        }
    }
}
 
 /* cd命令的内建函数 */
char* buildin_cd(uint32_t argc, char** argv) {
    if (argc > 2) {
        printf("cd: only support 1 argument!\n");
        return NULL;
    }

    /* 若是只键入cd而无参数,直接返回到根目录. */
    if (argc == 1) {
        final_path[0] = '/';
        final_path[1] = 0;
    } else {
        make_clear_abs_path(argv[1], final_path);
    }

    if (chdir(final_path) == -1) {
        printf("cd: no such directory %s\n", final_path);
        return NULL;
    }
    return final_path;
}
 
 /* ls命令的内建函数 */
void buildin_ls(uint32_t argc, char **argv) {
    char* pathname = NULL;
    struct stat file_stat;
    memset(&file_stat, 0, sizeof(struct stat));
    bool long_info = false;
    uint32_t arg_path_nr = 0;
    uint32_t arg_idx = 1; 
    // 参数解析
    while (arg_idx < argc) {
        if (argv[arg_idx][0] == '-') {
            // 如果是以-开头的参数，即选项
            if (!strcmp("-l", argv[arg_idx])) {         
                // 如果是参数-l
                long_info = true;
            } else if (!strcmp("-h", argv[arg_idx])) {   
                // 参数-h
                printf("usage: -l list all infomation about the file.\n-h for help\nlist all files in the current dirctory if no option\n"); 
                return;
            } else {	
                // 只支持-h -l两个选项
                printf("ls: invalid option %s\nTry `ls -h' for more information.\n", argv[arg_idx]);
                return;
            }
        } else {	     
            // ls的路径参数
            if (arg_path_nr == 0) {
                pathname = argv[arg_idx];
                arg_path_nr = 1;
            } else {
                printf("ls: only support one path\n");
                return;
            }
        }
        arg_idx++;
    } 

    if (pathname == NULL) {	 
        // 如果没有输入路径参数则直接使用当前的工作路径
        if (NULL != getcwd(final_path, MAX_PATH_LEN)) {
            pathname = final_path;
        } else {
            printf("ls: getcwd for default path failed\n");
            return;
        }
    } else {
        // 将相对路径转换成绝对路径
        make_clear_abs_path(pathname, final_path);
        pathname = final_path;
    }
    // 获取路径对应的文件信息
    if (stat(pathname, &file_stat) == -1) {
        printf("ls: cannot access %s: No such file or directory\n", pathname);
        return;
    }
    if (file_stat.st_filetype == FT_DIRECTORY) {
        // 如果是目录，则遍历目录获取内容
        struct dir* dir = opendir(pathname);
        struct dir_entry* dir_e = NULL;
        char sub_pathname[MAX_PATH_LEN] = {0};
        uint32_t pathname_len = strlen(pathname);
        uint32_t last_char_idx = pathname_len - 1;
        memcpy(sub_pathname, pathname, pathname_len);
        if (sub_pathname[last_char_idx] != '/') {
            sub_pathname[pathname_len] = '/';
            pathname_len++;
        }
        rewinddir(dir);
        if (long_info) {
            // 如果含有-l选项
            char ftype;
            printf("total: %d\n", file_stat.st_size);
            while((dir_e = readdir(dir))) {
                ftype = 'd';
                if (dir_e->f_type == FT_REGULAR) {
                    ftype = '-';
                } 
                sub_pathname[pathname_len] = 0;
                strcat(sub_pathname, dir_e->filename);
                memset(&file_stat, 0, sizeof(struct stat));
                if (stat(sub_pathname, &file_stat) == -1) {
                    printf("ls: cannot access %s: No such file or directory\n", dir_e->filename);
                    return;
                }
                printf("%c  %d  %d  %s\n", ftype, dir_e->i_no, file_stat.st_size, dir_e->filename);
            }
        } else {
            // 不带-l参数
            while((dir_e = readdir(dir))) {
                printf("%s ", dir_e->filename);
            }
            printf("\n");
        }
        closedir(dir);
    } else {
        // 对应的是普通文件
        if (long_info) {
            printf("-  %d  %d  %s\n", file_stat.st_ino, file_stat.st_size, pathname);
        } else {
            printf("%s\n", pathname);  
        }
    }
}
 
/* ps命令内建函数 */
void buildin_ps(uint32_t argc, char **argv) {
    if (argc != 1) {
        printf("ps: no argument support!\n");
        return;
    }
    ps();
}
 
/* clear命令内建函数 */
void buildin_clear(uint32_t argc, char **argv) {
    if (argc != 1) {
        printf("clear: no argument support!\n");
        return;
    }
    clear();
}
 
/* mkdir命令内建函数 */
int32_t buildin_mkdir(uint32_t argc, char **argv) {
    int32_t ret = -1;
    if (argc != 2) {
        printf("mkdir: only support 1 argument!\n");
    } else {
        make_clear_abs_path(argv[1], final_path);
        /* 若创建的不是根目录 */
        if (strcmp("/", final_path)) {
            if (mkdir(final_path) == 0) {
                ret = 0;
            } else {
                printf("mkdir: create directory %s failed.\n", argv[1]);
            }
        }
    }
    return ret;
}
 
/* rmdir命令内建函数 */
int32_t buildin_rmdir(uint32_t argc, char** argv) {
    int32_t ret = -1;
    if (argc != 2) {
        printf("rmdir: only support 1 argument!\n");
    } else {
        make_clear_abs_path(argv[1], final_path);
        /* 若删除的不是根目录 */
        if (strcmp("/", final_path)) {
            if (rmdir(final_path) == 0) {
                ret = 0;
            } else {
                printf("rmdir: remove %s failed.\n", argv[1]);
            }
        }
    }
    return ret;
}
 
 /* rm命令内建函数 */
 int32_t buildin_rm(uint32_t argc, char** argv) {
    int32_t ret = -1;
    if (argc != 2) {
        printf("rm: only support 1 argument!\n");
    } else {
        make_clear_abs_path(argv[1], final_path);
        /* 若删除的不是根目录 */
        if (strcmp("/", final_path)) {
            if (unlink(final_path) == 0) {
                ret = 0;
            } else {
                printf("rm: delete %s failed.\n", argv[1]);
            }

        }
    }
    return ret;
 }
