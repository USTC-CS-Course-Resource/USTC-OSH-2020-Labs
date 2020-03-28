#ifndef __COMMAND_H
#define __COMMAND_H

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

#define INPUT_SIZE 256
#define PWD_SIZE 4096
#define CMD_SIZE 256
#define ARG_NUM 128
#define ARG_SIZE 256
#define CMD_NUM 128
#define REDIR_NUM 10
#define TEMPFILE_NUM 10
#define LINE_SIZE 4096

#define true 1
#define false 0


typedef struct redirection {
    int fd[2];
    int mode;
    int toclose;
    char* tempfile;
} redirection;

typedef struct command {
    char* raw_cmd;
    char** args; // without redirs
    int argc;
    redirection** redirs;
    int redirc;
    char* file_not_found;
} command;

void split_strs(const char* str, const char* sep, /* in */
                char** strs, int* strc /* out */);
int pattern_recogize(const char* arg, const char* sep);
redirection* check_redir(const char* arg, const char* nextarg);
int redir(command* cmd);
command* deal_cmd(const char* raw_cmd);
int close_unlink_redir(command* cmd);
#endif