/*
 * TODO: 重定向多个的选择
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <setjmp.h>
#include <sys/socket.h>
#include "command.h"

#define INPUT_SIZE 256
#define PWD_SIZE 4096
#define CMD_SIZE 256
#define ARG_NUM 128
#define CMD_NUM 128

#define true 1
#define false 0

int exec_cmd(command* cmd, int will_fork);
void sig_handler(int signo);

jmp_buf jump_buffer;
pid_t sh_pid;
int i = 1;
int main() {
    /* current work directory */
    char cwd[PWD_SIZE];
    /* count for the commands */
    int cmdc = 0;
    /* set SIGINT for Ctrl+C */
    sh_pid = getpid();


    /* jumpback if get SIGINT*/
    sigsetjmp(jump_buffer, 1);
    signal(SIGINT, sig_handler);
    while (i++) {
        /* the input from the shell */
        char* input = (char*)calloc(INPUT_SIZE, sizeof(char));
        /* command prompt */
        getcwd(cwd, PWD_SIZE);
        fflush(stdout);
        printf("\033[1m\033[32mRabbit%d\033[0m:\033[1m\033[35m%s\033[0m# ", i, cwd);
        
        /* get the input */
        fflush(stdin);
        fgets(input, INPUT_SIZE, stdin);
        
        /* check if pipe isn't used in input */
        command* cmd;
        if(!strstr(input, "|")) {
            if(!(cmd = deal_cmd(input))) {
                continue;
            }
            exec_cmd(cmd, true);
            close_unlink_redir(cmd);
            continue;
        }

        /* pipe used in input */
        /* get the commands */
        char** cmds = (char**)calloc(CMD_NUM, sizeof(char*));
        int cmdc = 0;
        split_strs(input, "|\n", cmds, &cmdc);

        /* initialize the pipes */
        int fds[cmdc-1][2];
        for(int i = 0; i < cmdc-1; i++) {
            pipe(fds[i]);
        }
        
        /* execute and connect the pipes */
        for(int i = 0; i < cmdc; i++) {
            /* get split args of each command */
            char** args = (char**)calloc(ARG_NUM, sizeof(char*));
            int argc = 0;

            command* cmd = deal_cmd(cmds[i]);
            pid_t pid = fork();
            if(pid < 0) {
                printf("error: can't fork!\n");
                exit(2);
            }
            else if(pid == 0) {
                if(!cmd) {
                    printf("here\n");
                }
                /* connect the pipes */
                if(i != 0) {
                    dup2(fds[i-1][0], 0);
                    close(fds[i-1][0]);
                }
                if(i < cmdc - 1) {
                    dup2(fds[i][1], 1);
                }
                /* execute the command */
                if(exec_cmd(cmd, false) == -1) {
                    exit(255);
                }
            }
            else {
                waitpid(pid, NULL, 0);
                if(!cmd) {
                    break;
                }
                close_unlink_redir(cmd);
                if(i < cmdc - 1) {
                    close(fds[i][1]);
                }
            }
            free(args);
        }
        free(cmds);
    }

    return 0;
}

/* Describe: execute a command, but without pipe "|".
 *  But <, >, >> are allowed.
 * Input:
 *  cmd: the command to be execute
 *  will_fork: whether the external command will execute in a child process
 * Return:
 *  -1: file isn't found
 *  0: normal
 */ 
int exec_cmd(command* cmd, int will_fork) {
    /* no command */
    if (!cmd->args[0])
        return 0;

    /* built-in command */
    if (strcmp(cmd->args[0], "cd") == 0) {
        printf("cd!\n");
        if (cmd->args[1])
            chdir(cmd->args[1]);
        return 0;
    }
    if (strcmp(cmd->args[0], "pwd") == 0) {
        char wd[CMD_SIZE];
        puts(getcwd(wd, CMD_SIZE));
        return 0;
    }
    if (strcmp(cmd->args[0], "export") == 0) {
        for (int i = 1; cmd->args[i] != NULL; i++) {
            /* deal with each variables */
            char *name = cmd->args[i];
            char *value = cmd->args[i] + 1;
            while (*value != '\0' && *value != '=')
                value++;
            *value = '\0';
            value++;
            setenv(name, value, 1);
        }
        return 0;
    }
    if (strcmp(cmd->args[0], "exit") == 0)
        exit(0);

    
    /* external command */
    if(will_fork) {
        signal(SIGINT, sig_handler);
        pid_t pid = fork();
        if (pid == 0) {
            redir(cmd);
            execvp(cmd->args[0], cmd->args);
            exit(255);
        }
        else {
            signal(SIGINT, SIG_IGN);
            waitpid(pid, NULL, 0);
            signal(SIGINT, sig_handler);
        }
    }
    else {
        redir(cmd);
        execvp(cmd->args[0], cmd->args);
        exit(255);
    }
    return 0;
}

/*
 * the signal handler
 */ 
void sig_handler(int signo)
{
    printf("haha\r\n");
    if(signo ==  SIGINT)
    {
        if(sh_pid == getpid()) {
            siglongjmp(jump_buffer, 2);
        }
        else {
            exit(1);
        }
    }
}
