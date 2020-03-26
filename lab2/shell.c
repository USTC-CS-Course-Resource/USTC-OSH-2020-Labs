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

#define INPUT_SIZE 256
#define PWD_SIZE 4096
#define CMD_SIZE 256
#define ARG_NUM 128
#define CMD_NUM 128

#define true 1
#define false 0

void split_strs(char* str, const char* sep, /* in */
                char** strs, int* strc /* out */);
int exec_cmd(char* cmd, int will_fork);
int redirect_pre(char* cmd, char* out_fname, char* in_fname, int* out_flag, int* in_flag);
int redirect(char* out_fname, char* in_fname, int out_flag, int in_flag);
int checkfile(char* cmd);
char* get_parameter(char* str, const char* para, int keep);
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
        if(!strstr(input, "|")) {
            if(checkfile(input) == false) {
                continue;
            }
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
                if(checkfile(cmds[i]) == false) {
                    exit(255);
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
                if(exec_cmd(cmds[i], false) == -1) {
                    exit(255);
                }
            }
            else {
                waitpid(pid, NULL, 0);
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

/*
 * Describe: get the value of according parameter in str,
 *  and this parameter and the value will be removed from str 
 *  if keep == true
 */
char* get_parameter(char* str, const char* para, int keep) {
    char chars[] = " \t\n";
    char temp[2] = "a";
    char* pvalue;
    if(pvalue = strstr(str, para)) {
        if(keep == false) {
            *pvalue = '\0';
        }
        pvalue += strlen(para);
        
        int len = strlen(pvalue);
        for(int i = 0; i < len; i++) {
            temp[0] = pvalue[0];
            if(strstr(chars, temp)) {
                pvalue++;
            }
            else {
                break;
            }
        }
        char* others = pvalue;
        len = strlen(others);
        for(int i = 0; i < len; i++, others++) {
            temp[0] = others[0];
            if(strstr(chars, temp)) {
                others[0] = '\0';
                break;
            }
        }
        char* final_value = (char*)calloc(strlen(pvalue)+1, sizeof(char));
        final_value = strcpy(final_value, pvalue);
        others[0] = ' ';
        if(keep == true) {
            return final_value;
        }
        strcat(str, others);

        return final_value;
    }
    return NULL;
}

/*
 * Describe: redirect according to the parameters
 */
int redirect(char* out_fname, char* in_fname, int out_flag, int in_flag) {
    int fd;
    /* if >, >>, < is used */
    if(in_flag == 1) {
        close(0);
        fd = open(in_fname, O_RDONLY);
    }
    if(out_flag == 2) {
        close(1);
        fd = open(out_fname, O_RDWR|O_CREAT|O_APPEND, 0666);
    }
    else if(out_flag == 1) {
        close(1);
        fd = open(out_fname, O_WRONLY|O_CREAT|O_TRUNC, 0666);
    }
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
int exec_cmd(char* cmd, int will_fork) {
    /* check if <, >, >> in command */
    char* out_fname = NULL;
    char* in_fname = NULL;
    int out_flag = 0, in_flag = 0;

    if(out_fname = get_parameter(cmd, ">>", false)) {
        out_flag = 2;
    }
    else if(out_fname = get_parameter(cmd, ">", false)) {
        out_flag = 1;
    }
    if(in_fname = get_parameter(cmd, "<", false)) {
        in_flag = 1;
    }

    /* dealing command */
    int argc = 0;
    char** args = (char**)calloc(ARG_NUM, sizeof(char*));
    split_strs(cmd, " \n", args, &argc);
    /* no command */
    if (!args[0])
        return 0;

    /* built-in command */
    if (strcmp(args[0], "cd") == 0) {
        printf("cd!\n");
        if (args[1])
            chdir(args[1]);
        return 0;
    }
    if (strcmp(args[0], "pwd") == 0) {
        char wd[CMD_SIZE];
        puts(getcwd(wd, CMD_SIZE));
        return 0;
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
        return 0;
    }
    if (strcmp(args[0], "exit") == 0)
        exit(0);

    
    /* external command */
    if(will_fork) {
        signal(SIGINT, sig_handler);
        pid_t pid = fork();
        if (pid == 0) {
            printf("cmd{%s}\n", cmd);
            if(redirect(out_fname, in_fname, out_flag, in_flag) == -1) exit(255);
            execvp(args[0], args);
            exit(255);
        }
        else {
            signal(SIGINT, SIG_IGN);
            waitpid(pid, NULL, 0);
            signal(SIGINT, sig_handler);
        }
    }
    else {
        if(redirect(out_fname, in_fname, out_flag, in_flag) == -1) exit(255);
        execvp(args[0], args);
        exit(255);
    }
    return 0;
}

/*
 * Describe: checkout whether the files needed exist
 * Input:
 *  cmd: the command to be checked
 * Return
 *  true: exist
 *  false: not exist
 */ 
int checkfile(char* cmd) {
    char* cmd_cpy = (char*)calloc(strlen(cmd), sizeof(char));
    char* in_fname = NULL;
    if(in_fname = get_parameter(cmd, "<", true)) {
        int fd;
        fd = open(in_fname, O_RDONLY);
        if(fd != -1) {
            close(fd);
            return true;
        }
        else {
            close(fd);
            printf("The file {%s} isn't found!\n", in_fname);
            return false;
        }
    }
    return true;
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
