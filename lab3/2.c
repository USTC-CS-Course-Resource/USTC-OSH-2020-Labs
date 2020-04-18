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
#define BUF_SIZE 1048576

#define USER_SIZE 32
#define true 1
#define false 0

int accept_fds[USER_SIZE];
int user_num = 0;
int buffer[BUF_SIZE];

/* mutexes and conds */
pthread_mutex_t accept_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t send_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t accept_cond = PTHREAD_COND_INITIALIZER;

void *handle_chat(void *data);
int* fdalloc(int* fds);

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
    
    while(1) {
        /* 阻塞等待accept */
        int new_accept = accept(fd, NULL, NULL);
        /* 加accept锁 */
        pthread_mutex_lock(&accept_mutex);
        /* 等待accept条件变量 */
        while(user_num >= USER_SIZE) {
            pthread_cond_wait(&accept_cond, &accept_mutex);
        }
        int* accept_fd = fdalloc(accept_fds);
        *accept_fd = new_accept;
        if(*accept_fd == -1) {
            perror("accept");
            return 1;
        }
        user_num++;
        pthread_t thread;
        printf("<<<<<<<<<<<<<<<<<<<<A user(fd: %d) has connnected! The current user_num: %d Byte(s).\n", *accept_fd, user_num);
        pthread_create(&thread, NULL, handle_chat, (void*)accept_fd);
        pthread_detach(thread);
        pthread_mutex_unlock(&accept_mutex);
    }
    
    return 0;
}

void *handle_chat(void *data) {
    int* pfrom = (int*)data;
    int from = *(int*)data;
    char prompt[30];
    sprintf(prompt, "Message(from %d): ", from);
    char buffer[BUF_SIZE+1] = {0};
    ssize_t recv_size;
    int finish = true;
    ssize_t recv_size_accu = 0;
    while ((recv_size = recv(from, buffer, BUF_SIZE, 0)) > 0) {
        int temp_finish = finish;
        recv_size_accu += recv_size;
        printf("Server received %ld Byte(s) from %d. The cumulative received: %ld\n", recv_size, from, recv_size_accu);
        
        /* 加发送锁 */
        pthread_mutex_lock(&send_mutex);
        for(int i = 0; i < USER_SIZE; i++) {
            if(accept_fds[i] == 0 || accept_fds[i] == from) continue;
            temp_finish = finish;
            char* message = buffer;
            char* p = NULL;
            printf("<<<<<<<sending from %d to %d\n", from, accept_fds[i]);
            while(true) {
                if(temp_finish == true) {
                    /* 若上一次发送已经结束, 则在此发送新"Message:"提示 */
                    temp_finish = false;
                    send(accept_fds[i], prompt, strlen(prompt), 0);
                }
                if((p = strchr(message, '\n')) != NULL) {
                    /* 说明整个message中还存在换行, 在pos处 */
                    size_t message_len = (size_t)(p-message);
                    size_t send_len = send(accept_fds[i], message, (size_t)(p-message), 0);
                    printf("\tif  : %ld Byte(s) should be transmitted. %ld Byte(s) has been transmitted.\n", message_len, send_len);
                    send(accept_fds[i], "\n", 1, 0);
                    message = p + 1;
                    temp_finish = true;
                    if(message >= buffer + recv_size)  break;
                }
                else {
                    /* 整条message中不存在换行符, 直接发送, 并break */
                    size_t message_len = strlen(message);
                    size_t send_len = send(accept_fds[i], message, strlen(message), 0);
                    printf("\telse: %ld Byte(s) should be transmitted. %ld Byte(s) has been transmitted.\n", message_len, send_len);
                    temp_finish = false;
                    break;
                }
            }
            printf("Transmitting from %d to %d finished.>>>>>>>\n", from, accept_fds[i]);
        }
        finish = temp_finish;
        memset(buffer, 0, sizeof(char)*(BUF_SIZE+1));
        /* 解发送锁 */
        pthread_mutex_unlock(&send_mutex);
    }
    pthread_mutex_lock(&accept_mutex);
    *pfrom = 0;
    user_num--;
    close(from);
    printf("User(fd:%d) has exited! The current user_num: %d.>>>>>>>>>>>>>>>>>>>>\n", from, user_num);
    pthread_cond_signal(&accept_cond);
    pthread_mutex_unlock(&accept_mutex);
    return NULL;
}

int* fdalloc(int* fds) {
    for(int i = 0; i < USER_SIZE; i++) {
        if(fds[i] == 0) {
            return fds + i;
        }
    }
    return NULL;
}