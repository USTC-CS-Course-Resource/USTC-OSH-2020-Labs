#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <fcntl.h>

/* Max message size in Byte */
#define MESSAGE_SIZE 1048576
#define BUF_SIZE 2048
#define SEND_BUF_SIZE 2048
#define RECV_BUF_SIZE 2048
#define true 1
#define false 0

typedef struct Pipe {
    int fd_send;
    int fd_recv;
} Pipe;


void *handle_chat(void *data) {
    struct Pipe *pipe = (struct Pipe *)data;
    char prompt[] = "Message: ";
    char buffer[BUF_SIZE+1] = {0};
    ssize_t recv_size;
    int finish = true;
    ssize_t recv_size_accu = 0;
    while ((recv_size = recv(pipe->fd_send, buffer, BUF_SIZE, 0)) > 0) {
        recv_size_accu += recv_size;
        char* message = buffer;
        char* p = NULL;
        while(true) {
            if(finish == true) {
                /* 若上一次发送已经结束, 则在此发送新"Message:"提示 */
                finish = false;
                send(pipe->fd_recv, prompt, strlen(prompt), 0);
            }
            if((p = strchr(message, '\n')) != NULL) {
                /* 说明整个message中还存在换行, 在pos处 */
                *p = '\0';
                size_t message_len = strlen(message);
                size_t sent_len = send(pipe->fd_recv, message, strlen(message), 0);
                send(pipe->fd_recv, "\n", 1, 0);
                message = p + 1;
                finish = true;
                if(message >= buffer + recv_size) {
                    memset(buffer, 0, sizeof(char)*(BUF_SIZE+1));
                    break;
                }
            }
            else {
                /* 整条message中不存在换行符, 直接发送, 并break */
                size_t message_len = strlen(message);
                size_t sent_len = send(pipe->fd_recv, message, strlen(message), 0);
                memset(buffer, 0, sizeof(char)*(BUF_SIZE+1));
                finish = false;
                break;
            }
        }   
    }
    return NULL;
}


int main(int argc, char **argv) {
    int port = atoi(argv[1]);
    int fd;

    if ((fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket");
        return 1;
    }
    
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
    
    int fd1 = accept(fd, NULL, NULL);
    int fd2 = accept(fd, NULL, NULL);
    if (fd1 == -1 || fd2 == -1) {
        perror("accept");
        return 1;
    }
    pthread_t thread1, thread2;
    struct Pipe pipe1;
    struct Pipe pipe2;
    pipe1.fd_send = fd1;
    pipe1.fd_recv = fd2;
    pipe2.fd_send = fd2;
    pipe2.fd_recv = fd1;
    pthread_create(&thread1, NULL, handle_chat, (void *)&pipe1);
    pthread_create(&thread2, NULL, handle_chat, (void *)&pipe2);
    pthread_join(thread1, NULL);
    pthread_join(thread2, NULL);
    close(fd);
    return 0;
}