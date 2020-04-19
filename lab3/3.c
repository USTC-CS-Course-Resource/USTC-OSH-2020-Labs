#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <linux/types.h>

#define MESSAGE_SIZE 1048576
#define BUF_SIZE 1048576

#define USER_SIZE 1
#define true 1
#define false 0

int user_num = 0;
int accept_fds[USER_SIZE] = {0};
struct timeval timeout = {1, 0};

int* fdalloc(int* fds);
int get_max_fd(int* fds);
int try_accept(int fd, fd_set* clients);
int update_accept_fds(fd_set* clients);

int main(int argc, char **argv) {
    int port = atoi(argv[1]);
    int fd;
    if ((fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket");
        return 1;
    }
    int on = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(int));
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(int));

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    socklen_t addr_len = sizeof(addr);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr))) {
        perror("bind");
        return 1;
    }
    if (listen(fd, 2)) {
        perror("listen");
        return 1;
    }

    fd_set clients; // 文件描述符集合
    FD_ZERO(&clients);

    while(1) {
        try_accept(fd, &clients);
        if(select(get_max_fd(accept_fds) + 1, &clients, NULL, NULL, &timeout) > 0) {
            for(int i = 0; i < USER_SIZE; i++) {
                if(accept_fds[i] == 0) continue;
                if (FD_ISSET(accept_fds[i], &clients)) {
                    printf("%d is still alive!\n", accept_fds[i]);
                }
                else {
                    accept_fds[i] = 0;
                }
            }
        }
    }
    return 0;
}  

int forward() {
    
}

int try_accept(int fd, fd_set* clients) {
    update_accept_fds(clients);
    int* accept_fd = fdalloc(accept_fds);
    if(accept_fd == NULL) return -1;

    char prompt[50];
    int new_accept = 0;

    new_accept = accept(fd, NULL, NULL);
    sprintf(prompt, "[Server] Connecting...\n");
    send(new_accept, prompt, strlen(prompt), 0);
    
    fcntl(new_accept, F_SETFL, fcntl(new_accept, F_GETFL, 0) | O_NONBLOCK);
    FD_SET(new_accept, clients);
    *accept_fd = new_accept;
    user_num++;
    sprintf(prompt, "[Server] Connect successfully! Your fd is: %d\n", new_accept);
    send(new_accept, prompt, strlen(prompt), 0);
    printf("<<<<<<<<<<<<<<<<<<<<A user(fd: %d) has connnected! The current user_num: %d Byte(s).\n", *accept_fd, user_num);
    new_accept = 0;
}

int update_accept_fds(fd_set* clients) {
    int keepAlive = 1;
    for(int i = 0; i < USER_SIZE; i++) {
        char temp[32]; 
        if(accept_fds[i] == 0) continue;
        ssize_t recv_size = 0;
        if((recv_size = recv(accept_fds[i], temp, sizeof(temp), MSG_PEEK)) <= 0 && recv_size != -1) {
            printf("User(fd:%d) has exited! The current user_num: %d.>>>>>>>>>>>>>>>>>>>>\n", accept_fds[i], user_num);
            close(accept_fds[i]);
            accept_fds[i] = 0;
            user_num--;
        }
    }
}

int* fdalloc(int* fds) {
    for(int i = 0; i < USER_SIZE; i++) {
        if(fds[i] == 0) {
            return fds + i;
        }
    }
    return NULL;
}

int get_max_fd(int* fds) {
    int max_fd = 0;
    for(int i = 0; i < USER_SIZE; i++) {
        if(max_fd < fds[i]) {
            max_fd = fds[i];
        }
    }
    return max_fd;
}