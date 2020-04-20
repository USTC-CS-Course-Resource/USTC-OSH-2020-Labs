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

#define USER_SIZE 32
#define true 1
#define false 0

int user_num = 0;
int accept_fds[USER_SIZE] = {0};
struct timeval timeout = {0, 0};
int finishes[USER_SIZE];
int BUF_SIZE = 16384;

int* fdalloc(int* fds);
int get_max_fd(int* fds);
int try_accept(int fd);
int update_accept_fd(int index);
int forward();
int send_till_ok(int fd_index, char* begin, char* end);

int main(int argc, char **argv) {
    int port = atoi(argv[1]);
    int fd;
    if ((fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket");
        return 1;
    }
    int on = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(int));
    int opt_val = 0;
    int opt_len = sizeof(opt_len);
    getsockopt(fd, SOL_SOCKET, SO_SNDBUF, (char*)&opt_val, &opt_len);
    BUF_SIZE = opt_val;
    getsockopt(fd, SOL_SOCKET, SO_RCVBUF, (char*)&opt_val, &opt_len);
    BUF_SIZE = opt_val > BUF_SIZE ? BUF_SIZE : opt_val;

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
    while(true) {
        if(!forward()) try_accept(fd);
    }
    return 0;
}  

int forward() {
    int sending = false;
    for(int i = 0; i < USER_SIZE; i++) {
        if(accept_fds[i] == 0) continue;
        int from = accept_fds[i];
        /* select 测试是否有可以接受数据的客户端 */
        fd_set* client = (fd_set*)malloc(sizeof(fd_set));
        FD_ZERO(client);
        FD_SET(from, client);
        if(select(from + 1, client, NULL, NULL, &timeout) > 0) {
            char prompt[30];
            sprintf(prompt, "[Message(from %d)] ", from);
            char* buffer = (char*)calloc(BUF_SIZE + 1, sizeof(char));
            if(FD_ISSET(from, client)) {
                /* try recv */
                ssize_t recv_size = recv(from, buffer, BUF_SIZE, 0);
                if(recv_size <= 0) continue;
                sending = true;
                int temp_finish = finishes[i];
                printf("[Server] received %ld Byte(s) from %d.\n", recv_size, from);
                /* 开始转发 */
                for(int j = 0; j < USER_SIZE; j++) {
                    if(accept_fds[j] == 0 || accept_fds[j] == from) continue;
                    int to = accept_fds[j];
                    fd_set* wrtiefds = (fd_set*)malloc(sizeof(fd_set));
                    FD_ZERO(wrtiefds); FD_SET(to, wrtiefds);
                    while(1) {
                        if(select(to + 1, NULL, wrtiefds, NULL, NULL) > 0) {
                            if(FD_ISSET(to, wrtiefds)) break;
                        }
                    }
                    temp_finish = finishes[i];
                    char* message = buffer;
                    char* p = NULL;
                    printf("<<<<<sending from %d to %d\n", from, to);
                    while(true) {
                        if(temp_finish == true) {
                            /* 若上一次发送已经结束, 则在此发送新"Message:"提示 */
                            temp_finish = false;
                            send(to, prompt, strlen(prompt), MSG_NOSIGNAL);
                        }
                        if((p = strchr(message, '\n')) != NULL) {
                            /* 说明整个message中还存在换行, 在pos处 */
                            send_till_ok(j, message, p);
                            char tempstr[] = "\n";
                            send_till_ok(j, tempstr, tempstr + 1);
                            message = p + 1;
                            temp_finish = true;
                            if(message >= buffer + recv_size) break;
                        }
                        else {
                            /* 整条message中不存在换行符, 直接发送, 并break */
                            size_t message_len = strlen(message);
                            send_till_ok(j, message, message + message_len);
                            temp_finish = false;
                            break;
                        }
                    }
                    printf("Transmitting from %d to %d finished.>>>>>\n", from, to);
                }
                finishes[i] = temp_finish;
                memset(buffer, 0, sizeof(char)*(BUF_SIZE+1));
            }
            free(buffer);
        }    
        free(client);    
    }
    return sending;
}

int send_till_ok(int fd_index, char* begin, char* end) {
    size_t message_len = (size_t)(end - begin);
    int to = accept_fds[fd_index];
    ssize_t send_len = 0;
    /* 循环发送直到发送的数目正确了 */
    while(true) {
        /* 尝试发送 */
        send_len = send(to, begin, (size_t)(end-begin), MSG_NOSIGNAL);
        /* 若发现客户端已经断开, 则马上停止发送 */
        if(update_accept_fd(fd_index) <= 0) break;
        /* 移动begin指针到将继续发送的剩余部分 */
        begin = send_len == -1 ? begin : begin + send_len;
        if(begin == end) break;
    }
    printf("\t%ld Byte(s) / %ld Byte(s) transmitted.\n", send_len, message_len);
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
    printf("<<<<<<<<<<<<<<<<<<<<A user(fd: %d) has connnected! The current user_num: %d.\n", *accept_fd, user_num);

    if(update_accept_fd(accept_fd-accept_fds) == -1) return -1;

    char prompt[50];
    sprintf(prompt, "[Server] Connecting...\n");
    send(new_accept, prompt, strlen(prompt), MSG_NOSIGNAL);
    sprintf(prompt, "[Server] Connect successfully! Your fd is: %d\n", new_accept);
    send(new_accept, prompt, strlen(prompt), MSG_NOSIGNAL);
}

int update_accept_fd(int index) {
    if(accept_fds[index] == 0) return 0;
    char temp[32]; 
    ssize_t recv_size = 0;
    extern int errno;
    recv_size = recv(accept_fds[index], temp, sizeof(temp), MSG_PEEK | MSG_DONTWAIT);
    //printf("错误代码是: %d的recv_size=%d, errno=%d\n", accept_fds[index], recv_size, errno);
    if(((errno == EPIPE || errno == ECONNRESET || errno == EBADF) && recv_size < 0) || recv_size == 0) {
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
    static int from = -1;
    from = (from + USER_SIZE + 1) % USER_SIZE;
    for(int i = from; i < USER_SIZE; i++) {
        if(fds[i] == 0) {
            return fds + i;
        }
    }
    for(int i = 0; i < from; i++) {
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