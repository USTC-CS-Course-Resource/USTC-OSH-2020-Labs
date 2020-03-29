#include "command.h"

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
void split_strs(const char* str, const char* sep, /* in */
                char** strs, int* strc /* out */) {
    *strc = 0;
    char* s = (char*)calloc(strlen(str), sizeof(char));
    strcpy(s, str);
    char* token = strtok(s, sep);
    while(token != NULL) {
        strs[(*strc)++] = token;
        token = strtok(NULL, sep);
    }
    strs[*strc] = NULL;
}

/*
 * Return:
 *  error: -1
 *  1: < (filename)
 *  2: x<
 *  3: x<&y
 */
int pattern_recogize(const char* arg, const char* sep) {
    char* suffix;
    suffix = strstr(arg, sep);
    if(!suffix) return -1;
    int suffix_len = strlen(suffix);
    int sep_len = strlen(sep);
    int arg_len = strlen(arg);
    if(suffix_len == sep_len && arg_len == sep_len) {
        return 1;
    }
    else if(suffix_len == sep_len) {
        return 2;
    }
    else {
        return 3;
    }
    return -1;
}

/*
 * Note: there must be a ' ' between operator and string.
 *  if "<<<" or "<<" is used, the pipe won't work.
 * Return:
 *  -1: error
 * `x>> filename`, `x>>y`, `>> filename`
 * `x>&y`
 * `x> filename`, `x>y`, `> filename`
 * `x<<< filename`
 * `x<< filename`
 * `x<&y`
 * `x< filename`, `x<y`, `< filename`
 */ 
redirection* check_redir(const char* arg, const char* nextarg) {
    redirection* redir = (redirection*)malloc(sizeof(redirection));
    redir->toclose = 0;

    /* set fd for >> */
    if(strstr(arg, ">>")) {
        redir->mode = 1;
        redir->fd[1] = 1;
        int ptn = pattern_recogize(arg, ">>");
        if(ptn == 1 || ptn == 2) {
            if(ptn == 2) {
                if(sscanf(arg, "%d>>", &redir->fd[1]) < 1) {
                    printf("some error found around >\n");
                    redir->mode = -1;
                }
            }
            if(!nextarg) {
                printf("some error found around >>\n");
                redir->mode = -1;
            }
            else {
                redir->fd[0] = open(nextarg, O_WRONLY|O_CREAT|O_APPEND, 0666);
                redir->toclose = redir->fd[0];
            }
        }
        else if(ptn == 3) {
            if(sscanf(arg, "%d>>%d", &redir->fd[1], &redir->fd[0]) < 2) {
                printf("some error found around >>\n");
                redir->mode = -1;
            }
        }
        else {
            printf("error!\n");
            return NULL;
        }
        return redir;
    }
    /* set fd for >& */
    else if(strstr(arg, ">&")) {
        redir->mode = 2;
        if(sscanf(arg, "%d>&%d", &redir->fd[1], &redir->fd[0]) < 2) {
            printf("some error found around >&\n");
            redir->mode = -1;
        }
        return redir;
    }
    /* set fd for > */
    if(strstr(arg, ">")) {
        redir->mode = 3;
        redir->fd[1] = 1;
        int ptn = pattern_recogize(arg, ">");
        if(ptn == 1 || ptn == 2) {
            if(ptn == 2) {
                if(sscanf(arg, "%d>", &redir->fd[1]) < 1) {
                    printf("get %s\n", arg);
                    printf("some error found around >\n");
                    redir->mode = -1;
                }
            }
            int addr[5];
            /* check whether it's file or a /dev/tcp */
            if(sscanf(nextarg, "/dev/tcp/%d.%d.%d.%d/%d", &addr[0], &addr[1], &addr[2], &addr[3], &addr[4] ) == 5) {
                /* get address */
                char address[20] = {'\0'};
                sprintf(address, "%d.%d.%d.%d", addr[0], addr[1], addr[2], addr[3]);
                /* socket */
                int fd = socket(AF_INET, SOCK_STREAM/* for TCP */, 0);
                /* set sockaddr_in */
                struct sockaddr_in sin;  
                bzero(&sin, sizeof(sin));
                sin.sin_family = AF_INET;
                sin.sin_port = htons(addr[4]);
                inet_pton(AF_INET, address, &sin.sin_addr);
                //Connect to remote server
                if (connect(fd , (struct sockaddr *)&sin , sizeof(sin)) < 0)
                {
                    printf("cannot connect to %s/%d\n", address, addr[4]);
                    redir->mode = -1;
                }
                redir->fd[0] = fd;
            }
            else {
                if(!nextarg) {
                    printf("some error found around >\n");
                    redir->mode = -1;
                }
                else {
                    redir->fd[0] = open(nextarg, O_WRONLY|O_CREAT|O_APPEND, 0666);
                    redir->toclose = redir->fd[0];
                }
            }
            redir->toclose = redir->fd[0];
        }
        else if(ptn == 3) {
            if(sscanf(arg, "%d>%d", &redir->fd[1], &redir->fd[0]) < 2) {
                printf("some error found around >\n");
                redir->mode = -1;
            }
        }
        else {
            printf("error!\n");
            return NULL;
        }
        return redir;
    }
    /* set fd for <<< */
    else if(strstr(arg, "<<<")) {
        redir->mode = 4;
        redir->fd[1] = 0;
        /* create a tempfile to store the input */
        redir->tempfile = (char*)calloc(20, sizeof(char)); 
        strcpy(redir->tempfile, ".temp.XXXXXX");
	    redir->fd[0] = mkstemp(redir->tempfile);
        redir->fd[1] = 0;
        if(!nextarg) {
            printf("some error found around <<<\n");
            redir->mode = -1;
        }
        else {
            write(redir->fd[0], nextarg, strlen(nextarg));
            write(redir->fd[0], "\n", 1);
            close(redir->fd[0]);
            /* will redir later */
            redir->toclose = redir->fd[0];
            if((redir->fd[0] = open(redir->tempfile, O_RDONLY, 0666)) == -1) {
                printf("can not find the file: %s\n", nextarg);
                redir->mode = -1;
            }
        }
        return redir;
    }
    /* set fd for << */
    else if(strstr(arg, "<<")) {
        redir->mode = 5;
        redir->fd[1] = 0;
        /* create a tempfile to store the input */
        redir->tempfile = (char*)calloc(20, sizeof(char)); 
        strcpy(redir->tempfile, ".temp.XXXXXX");
	    redir->fd[0] = mkstemp(redir->tempfile);
        redir->fd[1] = 0;
        /* set the signal to compare with to decide whether to stop */
        if(!nextarg) {
            printf("some error found around <<\n");
            redir->mode = -1;
        }
        else {
            char* sig = (char*)calloc(strlen(nextarg)+2, sizeof(char));
            strcpy(sig, nextarg);
            sig[strlen(nextarg)] = '\n';
            sig[strlen(nextarg)+1] = '\0';
            /* input loop*/
            char* input = (char*)calloc(LINE_SIZE, sizeof(char));
            while(1) {
                printf("> ");
                fflush(stdin);
                fgets(input, LINE_SIZE, stdin);
                if(strcmp(input, sig) == 0) {
                    break;
                }
                write(redir->fd[0], input, strlen(input));
            }
            close(redir->fd[0]);
            /* will redir later */
            redir->toclose = redir->fd[0];
            if((redir->fd[0] = open(redir->tempfile, O_RDONLY, 0666)) == -1) {
                printf("can not find the file: %s\n", nextarg);
                redir->mode = -1;
            }
        }
        return redir;
    }
    /* set fd for <& */
    else if(strstr(arg, "<&")) {
        redir->mode = 6;
        if(sscanf(arg, "%d<&%d", &redir->fd[0], &redir->fd[1]) < 2) {
            printf("some error found around >\n");
            redir->mode = -1;
        }
        return redir;
    }
    /* set fd for < */
    else if(strstr(arg, "<")) {
        redir->mode = 7;
        redir->fd[1] = 0;
        int ptn = pattern_recogize(arg, "<");
        if(ptn == 1 || ptn == 2) {
            if(ptn == 2) {
                if(sscanf(arg, "%d<", &redir->fd[1]) < 1) {
                    printf("some error found around >\n");
                    redir->mode = -1;
                }
            }
            redir->toclose = redir->fd[0];
            int addr[5];
            /* check whether it's file or a /dev/tcp */
            if(!nextarg) {
                printf("some error found around <\n");
                redir->mode = -1;
            }
            else {
                if(sscanf(nextarg, "/dev/tcp/%d.%d.%d.%d/%d", &addr[0], &addr[1], &addr[2], &addr[3], &addr[4] ) == 5) {
                    /* get address */
                    char address[20] = {'\0'};
                    sprintf(address, "%d.%d.%d.%d", addr[0], addr[1], addr[2], addr[3]);
                    /* socket */
                    int fd = socket(AF_INET, SOCK_STREAM/* for TCP */, 0);
                    /* set sockaddr_in */
                    struct sockaddr_in sin;  
                    bzero(&sin, sizeof(sin));
                    sin.sin_family = AF_INET;
                    sin.sin_port = htons(addr[4]);
                    inet_pton(AF_INET, address, &sin.sin_addr);
                    //Connect to remote server
                    if (connect(fd , (struct sockaddr *)&sin , sizeof(sin)) < 0)
                    {
                        printf("cannot connect to %s/%d\n", address, addr[4]);
                        redir->mode = -1;
                    }
                    redir->fd[0] = fd;
                }
                else {
                    if(!nextarg) {
                        printf("some error found around <<\n");
                        redir->mode = -1;
                    }
                    else {
                        if((redir->fd[0] = open(nextarg, O_RDONLY, 0666)) == -1) {
                            printf("can not find the file: %s\n", nextarg);
                            redir->mode = -1;
                        }
                    }
                }
                redir->toclose = redir->fd[0];
            }
        }
        else if(ptn == 3) {
            if(sscanf(arg, "%d<%d", &redir->fd[1], &redir->fd[0]) < 2) {
                printf("some error found around >\n");
                redir->mode = -1;
            }
        }
        else {
            printf("error!\n");
            return NULL;
        }
        return redir;
    }
    return NULL;
}

/*
 * Deal the raw_cmd string into command struct, and return it.
 */ 
command* deal_cmd(const char* raw_cmd) {
    /* initialize and split*/
    command* cmd = malloc(sizeof(command));
    cmd->raw_cmd = (char*)calloc(strlen(raw_cmd), sizeof(char));
    strcpy(cmd->raw_cmd, raw_cmd);
    int argc = 0;
    cmd->argc = 0;
    cmd->redirc = 0;
    char** args = (char**)calloc(ARG_NUM, sizeof(char*));
    cmd->redirs = (redirection**)calloc(REDIR_NUM, sizeof(redirection*));
    cmd->args = (char**)calloc(ARG_NUM, sizeof(char*));
    split_strs(raw_cmd, " \t\n", args, &argc);
    
    /* pick out the redirection */
    for(int i = 0; i < argc; i++) {
        redirection* redir;
        if(redir = check_redir(args[i], args[i+1])) {
            if(redir->mode == -1) {
                return NULL;
            }
            cmd->redirs[cmd->redirc] = redir;
            cmd->redirc++;
            if(redir->mode != 2 && redir->mode != 6 ) i++;
        }
        else {
            cmd->args[cmd->argc] = (char*)calloc(strlen(args[i]), sizeof(char));
            strcpy(cmd->args[cmd->argc], args[i]);
            cmd->argc++;
        }
    }

    /* add empty command for strang situations */
    if(cmd->argc == 0) {
        cmd->args[0] = (char*)calloc(2, sizeof(char));
        cmd->args[0][0] = ' ';
    }
    return cmd;
}

/*
 * do the redirecting according to the cmd->redirs
 */ 
int redir(command* cmd) {
    for(int i = 0; i < cmd->redirc; i++) {
        dup2(cmd->redirs[i]->fd[0], cmd->redirs[i]->fd[1]);
    }
}

/*
 * do the closing and unlinking of the redirected file or something else.
 */ 
int close_unlink_redir(command* cmd) {
    for(int i = 0; i < cmd->redirc; i++) {
        /* unlink the tempfile */
        if(cmd->redirs[i]->tempfile) {
            unlink(cmd->redirs[i]->tempfile);
        }
        /* close the fd/file */
        if(cmd->redirs[i]->toclose) {
            close(cmd->redirs[i]->toclose);
        }
    }
}