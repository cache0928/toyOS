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
