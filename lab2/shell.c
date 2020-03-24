#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/types.h>

#define INPUT_SIZE 256
#define PWD_SIZE 4096
#define CMD_SIZE 256
#define ARG_NUM 128
#define CMD_NUM 128

#define true 1
#define false 0

void split_strs(char* str, const char* sep, /* in */
                char** strs, int* strc /* out */);
void exec_cmd(char* cmd, int will_fork);

int main() {
    /* the input from the shell */
    char input[INPUT_SIZE];
    /* save the commands if pipe is used */
    char** cmds = (char**)calloc(CMD_NUM, sizeof(char*));
    /* count for the commands */
    int cmdc = 0;

    while (1) {
        /* command prompt */
        char cwd[PWD_SIZE];
        getcwd(cwd, PWD_SIZE);
        printf("%s# ", cwd);

        /* get the input */
        fflush(stdin);
        fgets(input, INPUT_SIZE, stdin);

        /* check if pipe isn't used in input */
        if(!strstr(input, "|")) {
            exec_cmd(input, true);
            continue;
        }

        /* pipe used in input */
        /* get the commands */
        char** cmds = (char**)calloc(100, sizeof(char*));
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

            pid_t pid = fork();
            if(pid < 0) {
                printf("error: can't fork!\n");
                exit(2);
            }
            else if(pid == 0) {
                /* connect the pipes */
                if(i != 0) {
                    dup2(fds[i-1][0], 0);
                    close(fds[i-1][0]);
                }
                if(i < cmdc - 1) {
                    dup2(fds[i][1], 1);
                }

                /* execute the command */
                exec_cmd(cmds[i], false);
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

/* describe: this function is used for split the str by sep into strs, 
 * while the count of strs will be returned by strc
 * 
 * input:
 *  str: string to be split
 *  sep: split by sep
 * output:
 *  strs: the split strs
 *  strc: the count of split strs
 */ 
void split_strs(char* str, const char* sep, /* in */
                char** strs, int* strc /* out */) {
    *strc = 0;
    char* token = strtok(str, sep);
    while(token != NULL) {
        strs[(*strc)++] = token;
        token = strtok(NULL, sep);
    }
    strs[*strc] = NULL;
}

/* describe: execute a command, but without pipe "|"
 * input:
 *  cmd: the command to be execute
 *  will_fork: whether the external command will execute in a child process
 */ 
void exec_cmd(char* cmd, int will_fork) {
    int argc = 0;
    char** args = (char**)calloc(ARG_NUM, sizeof(char*));
    split_strs(cmd, " \n", args, &argc);
    /* no command */
    if (!args[0])
        return;

    /* built-in command */
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
            /* deal with each variables */
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

    /* external command */
    if(will_fork) {
        pid_t pid = fork();
        if (pid == 0) {
            /* child process */
            execvp(args[0], args);
            /* execvp failed */
            exit(255);
        }
        /* parent process */
        wait(NULL);
    }
    else {
        /* child process */
        execvp(args[0], args);
        /* execvp failed */
        exit(255);
    }
}