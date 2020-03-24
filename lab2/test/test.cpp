#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/wait.h>

void get_split_strs(char* str, char* sep, /* in */
                    char** strs, int &strc /* out */) {
    strc = 0;
    char* token = strtok(str, sep);
    while(token != NULL) {
        strs[strc++] = token;
        token = strtok(NULL, sep);
    }
    strs[strc] = NULL;
}

void free_strs(char** strs, int strc) {
    for(int i = 0; i < strc; i++) {
        free(strs[i]);
    }
    free(strs);
}

#if 0
int main()
{
    char cmd[] = "ls /home/rabbit | wc |wc";
    //char cmd[] = "env | wc";
    char** cmds;
    int cmdc = 0;
    char sep[] = "|\n";
    get_split_strs(cmd, sep, cmds, cmdc);

    int fds[cmdc-1][2];
    for(int i = 0; i < cmdc-1; i++) {
        pipe(fds[i]);
    }

    pid_t pid;

    for(int i = 0; i < cmdc; i++) {
        char** args;
        int argc = 0;
        char sep[] = " \n";

        pid_t pid = fork();


        if(pid < 0) {
            printf("error: can't fork!\n");
            return 1;
        }
        else if(pid == 0) {
            get_split_strs(cmds[i], sep, args, argc);
            
            if(i != 0) {
                dup2(fds[0][0], 0);
                char str[] = "adcdefghijklmnopqrstuvwxyz";
                read(fds[i-1][0], str, strlen(str)+1+1000);
                printf("get %s\n", str);
            }
            close(fds[0][0]);
            close(fds[0][1]);
            exit(0);
        }
        else {
            wait(NULL);
        }

        pipe(fds[0]);

        if(pid == 0) {
            printf("%d here\n %s\r\n", i, args[0]);
            dup2(fds[0][1], 1);
            execvp(args[0], args);
        }
        else {
            wait(NULL);
        }
    }
    return 0;
}
#endif

#if 1
int main()
{
    //char cmd[] = "ls /home/rabbit | wc |wc";
    char cmd[] = "env | wc";
    char** cmds = (char**)calloc(100, sizeof(char*));
    int cmdc = 0;
    char sep[] = "|\n";
    get_split_strs(cmd, sep, cmds, cmdc);
    int fds[cmdc-1][2];
    for(int i = 0; i < cmdc-1; i++) {
        pipe(fds[i]);
    }
    
    pid_t pid;
    
    for(int i = 0; i < cmdc; i++) {
        char** args = (char**)calloc(100, sizeof(char*));
        int argc = 0;
        char sep[] = " \n";
        pid_t pid = fork();

        if(pid < 0) {
            printf("error: can't fork!\n");
        }
        else if(pid == 0) {
            get_split_strs(cmds[i], sep, args, argc);
            
            if(i != 0) {
                dup2(fds[i-1][0], 0);
                close(fds[i-1][0]);
            }

            if(i < cmdc - 1) {
                dup2(fds[i][1], 1);
            }
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
    free(cmds);
    return 0;
}
#endif