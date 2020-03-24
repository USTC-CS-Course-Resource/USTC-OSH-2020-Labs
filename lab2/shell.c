#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/types.h>

#define PWD_SIZE 4096
#define CMD_SIZE 256
#define ARG_NUM 128
#define CMD_NUM 128

void split_strs(char* str, char* sep, /* in */
                char** strs, int* strc /* out */);
void exec_cmd(char* cmd);

int main() {
    /* 输入的命令行 */
    char input[CMD_SIZE];
    /* 命令行拆解成的各部分，以空指针结尾 */
    char** cmds = (char**)calloc(CMD_NUM, sizeof(char*));
    int cmdc = 0;

    while (1) {
        /* 提示符 */
        char cwd[PWD_SIZE];
        getcwd(cwd, PWD_SIZE);
        printf("%s# ", cwd);

        /* 读取输入 */
        fflush(stdin);
        fgets(input, CMD_SIZE, stdin);
        /* 判断input中是否使用了管道，如未使用则直接执行 */
        if(!strstr(input, "|")) {
            exec_cmd(input);
            continue;
        }
        /* input中使用了管道 */
        char** cmds = (char**)calloc(100, sizeof(char*));
        int cmdc = 0;
        char sep[] = "|\n";
        split_strs(input, sep, cmds, &cmdc);
        int fds[cmdc-1][2];
        for(int i = 0; i < cmdc-1; i++) {
            pipe(fds[i]);
        }
        
        pid_t pid;
        
        for(int i = 0; i < cmdc; i++) {
            char** args = (char**)calloc(ARG_NUM, sizeof(char*));
            int argc = 0;
            char sep[] = " \n";
            pid_t pid = fork();

            if(pid < 0) {
                printf("error: can't fork!\n");
            }
            else if(pid == 0) {
                if(i != 0) {
                    dup2(fds[i-1][0], 0);
                    close(fds[i-1][0]);
                }

                if(i < cmdc - 1) {
                    dup2(fds[i][1], 1);
                }

                exec_cmd(cmds[i]);
                execvp(args[0], args);
            }
            else {
                wait(NULL);
                if(i < cmdc - 1) {
                    close(fds[i][1]);
                }
            }
            free(args);
        }     
    }
    free(cmds);   

    return 0;
}

/* this function is used for split the str by sep into strs, 
 * while the count of strs will be returned by strc
 */ 
void split_strs(char* str, char* sep, /* in */
                char** strs, int* strc /* out */) {
    *strc = 0;
    char* token = strtok(str, sep);
    while(token != NULL) {
        strs[(*strc)++] = token;
        token = strtok(NULL, sep);
    }
    strs[*strc] = NULL;
}

void exec_cmd(char* cmd) {
    int argc = 0;
    char** args = (char**)calloc(ARG_NUM, sizeof(char*));
    char sep[] = " \n";
    split_strs(cmd, sep, args, &argc);
    /* 没有输入命令 */
    if (!args[0])
        return;

    /* 内建命令 */
    if (strcmp(args[0], "cd") == 0) {
        if (args[1])
            chdir(args[1]);
        return;
    }
    if (strcmp(args[0], "pwd") == 0) {
        char wd[CMD_SIZE];
        puts(getcwd(wd, CMD_SIZE));
        return;
    }
    if (strcmp(args[0], "export") == 0) {
        for (int i = 1; args[i] != NULL; i++) {
            /*处理每个变量*/
            char *name = args[i];
            char *value = args[i] + 1;
            while (*value != '\0' && *value != '=')
                value++;
            *value = '\0';
            value++;
            setenv(name, value, 1);
        }
        return;
    }
    if (strcmp(args[0], "exit") == 0)
        exit(0);

    /* 外部命令 */
    pid_t pid = fork();
    if (pid == 0) {
        /* 子进程 */
        execvp(args[0], args);
        /* execvp失败 */
        exit(255);
    }
    /* 父进程 */
    wait(NULL);
}