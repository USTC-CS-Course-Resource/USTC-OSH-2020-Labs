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
#include <errno.h>

#define MESSAGE_SIZE 1048576
#define BUF_SIZE 1048576

#define USER_SIZE 32
#define true 1
#define false 0

int user_num = 0;
int accept_fds[USER_SIZE] = {0};
struct timeval timeout = {0, 0};
int finishes[USER_SIZE];

int* fdalloc(int* fds);
int get_max_fd(int* fds);
int try_accept(int fd);
int update_accept_fd(int index);
int forward();

int main(int argc, char **argv) {
    int port = atoi(argv[1]);
    int fd;
    if ((fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket");
        return 1;
    }
    int on = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(int));

    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK); /* 设置服务器fd为非阻塞 */
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

    for(int i = 0; i < USER_SIZE; i++) finishes[i] = true;
    while(1) {
        if(!forward()) try_accept(fd);
    }
    return 0;
}  

int forward() {
    int sending = false;
    for(int i = 0; i < USER_SIZE; i++) {
        update_accept_fd(i);
        if(accept_fds[i] == 0) continue;
        /* select 测试是否有可以接受数据的客户端 */
        fd_set* client = (fd_set*)malloc(sizeof(fd_set));
        FD_ZERO(client);
        for(int i = 0; i < USER_SIZE; i++) {
            if(accept_fds[i] == 0) continue;
            FD_SET(accept_fds[i], client);
        }
        if(select(get_max_fd(accept_fds) + 1, client, NULL, NULL, &timeout) > 0) {
            int from = accept_fds[i];
            char prompt[30];
            sprintf(prompt, "[Message(from %d)] ", from);
            char buffer[BUF_SIZE+1] = {0};
            if(accept_fds[i] == 0) continue;
            if(FD_ISSET(accept_fds[i], client)) {
                /* try recv */
                ssize_t recv_size = recv(from, buffer, BUF_SIZE, 0);
                if(recv_size <= 0) continue;
                sending = true;
                int temp_finish = finishes[i];
                printf("[Server] received %ld Byte(s) from %d.\n", recv_size, from);
                /* 开始转发 */
                for(int j = 0; j < USER_SIZE; j++) {
                    update_accept_fd(j);
                    if(accept_fds[j] == 0 || accept_fds[j] == from) continue;
                    while(1) {
                        if(select(get_max_fd(accept_fds) + 1, NULL, client, NULL, NULL) > 0) {
                            if(FD_ISSET(accept_fds[i], client)) break;
                        }
                    }
                    temp_finish = finishes[i];
                    char* message = buffer;
                    char* p = NULL;
                    printf("<<<<<sending from %d to %d\n", from, accept_fds[j]);
                    while(true) {
                        if(temp_finish == true) {
                            /* 若上一次发送已经结束, 则在此发送新"Message:"提示 */
                            temp_finish = false;
                            send(accept_fds[j], prompt, strlen(prompt), 0);
                        }
                        if((p = strchr(message, '\n')) != NULL) {
                            /* 说明整个message中还存在换行, 在pos处 */
                            size_t message_len = (size_t)(p-message);
                            size_t send_len;
                            while((send_len = send(accept_fds[j], message, (size_t)(p-message), 0)) == -1) {
                                if(update_accept_fd(j) == -1) break;
                            }
                            printf("\t%ld Byte(s) / %ld Byte(s) transmitted.\n", message_len, send_len);
                            while(send(accept_fds[j], "\n", 1, 0) == -1) {
                                if(update_accept_fd(j) == -1) break;
                            }
                            message = p + 1;
                            temp_finish = true;
                            if(message >= buffer + recv_size) break;
                        }
                        else {
                            /* 整条message中不存在换行符, 直接发送, 并break */
                            size_t message_len = strlen(message);
                            size_t send_len;
                            while((send_len = send(accept_fds[j], message, strlen(message), 0)) == -1) {
                                if(update_accept_fd(j) == -1) break;
                            }
                            printf("\t%ld Byte(s) / %ld Byte(s) transmitted.\n", message_len, send_len);
                            temp_finish = false;
                            break;
                        }
                    }
                    printf("Transmitting from %d to %d finished.>>>>>\n", from, accept_fds[j]);
                }
                finishes[i] = temp_finish;
                memset(buffer, 0, sizeof(char)*(BUF_SIZE+1));
            }
        }        
    }
    return sending;
}

int try_accept(int fd) {
    for(int i = 0; i < USER_SIZE; i++) {
        update_accept_fd(i);
    }

    int* accept_fd = fdalloc(accept_fds);
    if(accept_fd == NULL) return -1;
    
    int new_accept = accept(fd, NULL, NULL);
    if(new_accept < 0) return -1;
    fcntl(new_accept, F_SETFL, fcntl(new_accept, F_GETFL, 0) | O_NONBLOCK); /* 设置客户端为非阻塞 */
    *accept_fd = new_accept;
    user_num++;
    if(update_accept_fd(accept_fd-accept_fds) == -1) return -1;

    char prompt[50];
    sprintf(prompt, "[Server] Connecting...\n");
    send(new_accept, prompt, strlen(prompt), 0);
    
    sprintf(prompt, "[Server] Connect successfully! Your fd is: %d\n", new_accept);
    send(new_accept, prompt, strlen(prompt), 0);
    printf("<<<<<<<<<<<<<<<<<<<<A user(fd: %d) has connnected! The current user_num: %d Byte(s).\n", *accept_fd, user_num);
}

int update_accept_fd(int index) {
    if(accept_fds[index]== 0) return 0;
    char temp[32]; 
    ssize_t recv_size = 0;
    if((recv_size = recv(accept_fds[index], temp, sizeof(temp), MSG_PEEK | MSG_DONTWAIT)) <= 0 && recv_size != -1) {
        user_num--;
        finishes[index] = true;
        printf("User(fd:%d) has exited! The current user_num: %d.>>>>>>>>>>>>>>>>>>>>\n", accept_fds[index], user_num);
        close(accept_fds[index]);
        accept_fds[index] = 0;
        return -1;
    }
    return accept_fds[index];
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