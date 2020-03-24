#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include<fcntl.h>

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
char* strip(char* str, const char* chars);


int main() {
    /* current work directory */
    char cwd[PWD_SIZE];
    /* count for the commands */
    int cmdc = 0;

    while (1) {
        /* the input from the shell */
        char* input = (char*)calloc(INPUT_SIZE, sizeof(char));
        /* command prompt */
        getcwd(cwd, PWD_SIZE);
        printf("\033[1m\033[32mRabbit\033[0m:\033[1m\033[35m%s\033[0m# ", cwd);
        
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
        free(cmds);
    }

    return 0;
}

/* Describe: this function is used for split the str by sep into strs, 
 *  while the count of strs will be returned by strc
 * 
 * Input:
 *  str: string to be split
 *  sep: split by sep
 * Output:
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

/* Describe: set suffixal char into '\0', 
 *  and move str to the first meaningful position.
 * Input:
 *  str: string to be stripped
 *  chars: the char to be stripped
 * Return:
 *  the stripped string.
 */ 
char* strip(char* str, const char* chars) {
    char temp[2] = "a";
    int len = strlen(str);
    for(int i = 0; i < len; i++) {
        temp[0] = str[0];
        if(strstr(chars, temp)) {
            str++;
        }
        else {
            break;
        }
    }
    len = strlen(str);
    for(int i = len-1; i >= 0; i--) {
        temp[0] = str[i];
        if(strstr(chars, temp)) {
            str[i] = '\0';
        }
        else {
            break;
        }
    }
    return str;
}

/* Describe: execute a command, but without pipe "|".
 *  But <, >, >> are allowed.
 * Input:
 *  cmd: the command to be execute
 *  will_fork: whether the external command will execute in a child process
 */ 
void exec_cmd(char* cmd, int will_fork) {
    /* check if <, >, >> in command */
    char* out_fname = NULL;
    int out_flag = 0;
    if(out_fname = strstr(cmd, ">>")) {
        *(out_fname) = '\0';
        out_fname = out_fname + 2;
        out_fname = strip(out_fname, " \n");
        out_flag = 2;
    }
    else if(out_fname = strstr(cmd, ">")) {
        *(out_fname) = '\0';
        out_fname = out_fname + 1;
        out_fname = strip(out_fname, " \n");
        out_flag = 1;
    }
    char* in_fname = NULL;
    int in_flag = 0;
    if(in_fname = strstr(cmd, "<")) {
        *(in_fname) = '\0';
        in_fname = in_fname + 1;
        in_fname = strip(in_fname, " \n");
        in_flag = 1;
    }

    /* dealing command */
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
    int fd;
    if(will_fork) {
        pid_t pid = fork();
        if (pid == 0) {
            /* if >, >>, < is used */
            if(out_flag == 2) {
                close(1);
                fd = open(out_fname, O_RDWR|O_CREAT|O_APPEND, 0666);
            }
            else if(out_flag == 1) {
                close(1);
                fd = open(out_fname, O_WRONLY|O_CREAT|O_TRUNC, 0666);
            }
            if(in_flag == 1) {
                close(0);
                fd = open(in_fname, O_RDONLY);
            }
            /* child process */
            execvp(args[0], args);
            /* execvp failed */
            exit(255);
        }
        else {
            /* parent process */
            wait(NULL);
        }
    }
    else {
        /* if >, >>, < is used */
        if(out_flag == 2) {
            close(1);
            fd = open(out_fname, O_RDWR|O_CREAT|O_APPEND, 0666);
        }
        else if(out_flag == 1) {
            close(1);
            fd = open(out_fname, O_WRONLY|O_CREAT|O_TRUNC, 0666);
        }
        if(in_flag == 1) {
            close(0);
            fd = open(in_fname, O_RDONLY);
        }
        /* child process */
        execvp(args[0], args);
        /* execvp failed */
        exit(255);
    }
}

