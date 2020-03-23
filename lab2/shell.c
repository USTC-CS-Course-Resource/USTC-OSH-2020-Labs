#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/types.h>

#define PWD_SIZE 4096
#define CMD_SIZE 256
#define ARG_NUM 128

int main() {
    /* 输入的命令行 */
    char cmd[CMD_SIZE];
    /* 命令行拆解成的各部分，以空指针结尾 */
    char *args[ARG_NUM];
    int i;

    while (1) {
        /* 提示符 */
        char cwd[PWD_SIZE] = getcwd(cwd, PWD_SIZE);
        printf("%s# ", cwd);

        /* 读取输入 */
        fflush(stdin);
        fgets(cmd, CMD_SIZE, stdin);
        /* 清理结尾的换行符 */
        for (i = 0; cmd[i] != '\n'; i++);
        cmd[i] = '\0';
        /* 拆解命令行 */
        args[0] = cmd;
        for (i = 0; *args[i]; i++)
            for (args[i+1] = args[i] + 1; *args[i+1]; args[i+1]++)
                if (*args[i+1] == ' ') {
                    *args[i+1] = '\0';
                    args[i+1]++;
                    break;
                }
        args[i] = NULL;

        /* 没有输入命令 */
        if (!args[0])
            continue;

        /* 内建命令 */
        if (strcmp(args[0], "cd") == 0) {
            if (args[1])
                chdir(args[1]);
            continue;
        }
        if (strcmp(args[0], "pwd") == 0) {
            char wd[CMD_SIZE];
            puts(getcwd(wd, CMD_SIZE));
            continue;
        }
        if (strcmp(args[0], "export") == 0) {
            for (i = 1; args[i] != NULL; i++) {
                /*处理每个变量*/
                char *name = args[i];
                char *value = args[i] + 1;
                while (*value != '\0' && *value != '=')
                    value++;
                *value = '\0';
                value++;
                setenv(name, value, 1);
            }
            continue;
        }
        if (strcmp(args[0], "exit") == 0)
            return 0;

        /* 外部命令 */
        pid_t pid = fork();
        if (pid == 0) {
            /* 子进程 */
            execvp(args[0], args);
            /* execvp失败 */
            return 255;
        }
        /* 父进程 */
        wait(NULL);
    }
}