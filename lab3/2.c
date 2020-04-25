#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <fcntl.h>
#include <errno.h>

/* Max message size in Byte */
#define MESSAGE_SIZE 1048576

#define USER_SIZE 32
#define true 1
#define false 0

int accept_fds[USER_SIZE];
int user_num = 0;
int BUF_SIZE = 0;

/* mutexes and conds */
pthread_mutex_t accept_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t send_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t accept_cond = PTHREAD_COND_INITIALIZER;

void *handle_chat(void *data);
int* fdalloc(int* fds);
int check_accept_fd(int index);
int get_index(int* fds, int key);
void print_fds();
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
        char prompt[50];
        sprintf(prompt, "[Server] Connecting...\n");
        send(new_accept, prompt, strlen(prompt), 0);
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
        sprintf(prompt, "[Server] Connect successfully! Your fd is: %d\n", new_accept);
        send(new_accept, prompt, strlen(prompt), MSG_NOSIGNAL);
        printf("<<<<<<<<<<<<<<<<<<<<A user(fd: %d) has connnected! The current user_num: %d.\n", *accept_fd, user_num);
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
    sprintf(prompt, "[Message(from %d)] ", from);
    char* buffer = (char*)calloc(BUF_SIZE + 1, sizeof(char));
    ssize_t recv_size;
    int finish = true;
    ssize_t recv_size_accu = 0;
    while((recv_size = recv(from, buffer, BUF_SIZE, 0)) > 0) {
        int temp_finish = finish;
        /* 加发送锁 */
        pthread_mutex_lock(&send_mutex);
        for(int i = 0; i < USER_SIZE; i++) {
            if(accept_fds[i] == 0 || accept_fds[i] == from) continue;
            temp_finish = finish;
            char* message = buffer;
            char* p = NULL;
            printf("<<<<<sending from %d to %d\n", from, accept_fds[i]);
            while(true) {
                if(temp_finish == true) {
                    /* 若上一次发送已经结束, 则在此发送新"Message:"提示 */
                    temp_finish = false;
                    send(accept_fds[i], prompt, strlen(prompt), MSG_NOSIGNAL);
                }
                if((p = strchr(message, '\n')) != NULL) {
                    /* 说明整个message中还存在换行, 在pos处 */
                    size_t message_len = (size_t)(p-message);
                    size_t send_len;
                    send_till_ok(i, message, p);
                    char tempstr[] = "\n";
                    send_till_ok(i, tempstr, tempstr + 1);
                    message = p + 1;
                    temp_finish = true;
                    if(message >= buffer + recv_size)  break;
                }
                else {
                    /* 整条message中不存在换行符, 直接发送, 并break */
                    size_t message_len = strlen(message);
                    send_till_ok(i, message, message + message_len);
                    temp_finish = false;
                    break;
                }
            }
            printf("Transmitting from %d to %d finished.>>>>>\n", from, accept_fds[i]);
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
    free(buffer);
    printf("User(fd:%d) has exited! The current user_num: %d.>>>>>>>>>>>>>>>>>>>>\n", from, user_num);
    pthread_cond_signal(&accept_cond);
    pthread_mutex_unlock(&accept_mutex);
    return NULL;
}

int send_till_ok(int fd_index, char* begin, char* end) {
    size_t message_len = (size_t)(end - begin);
    int to = accept_fds[fd_index];
    ssize_t send_len = 0;
    /* 循环发送直到发送的数目正确了 */
    while(true) {
        /* 尝试发送 */
        extern int errno;
        send_len = send(to, begin, (size_t)(end-begin), MSG_NOSIGNAL);
        printf("%d try: %ld, errno:%d\n", to, send_len, errno);
        /* 若发现客户端已经断开, 则马上停止发送 */
        if(errno == EPIPE || errno == ECONNRESET || errno == EBADF) break;
        if(check_accept_fd(fd_index) <= 0) break;
        /* 移动begin指针到将继续发送的剩余部分 */
        begin = send_len == -1 ? begin : begin + send_len;
        if(begin == end) break;
    }
    printf("\t%ld Byte(s) / %ld Byte(s) transmitted.\n", send_len, message_len);
}

int check_accept_fd(int index) {
    if(accept_fds[index]== 0) return 0;
    char temp[32]; 
    ssize_t recv_size = 0;
    extern int errno;
    recv_size = recv(accept_fds[index], temp, sizeof(temp), MSG_PEEK | MSG_DONTWAIT);
    //printf("错误代码是: %d的recv_size=%d, errno=%d\n", accept_fds[index], recv_size, errno);
    if(((errno == EPIPE || errno == ECONNRESET || errno == EBADF) && recv_size < 0) || recv_size == 0) {
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

int get_index(int* fds, int key) {
    for(int i = 0; i < USER_SIZE; i++) {
        if(accept_fds[i] == key) return i;
    }
    return -1;
}

void print_fds() {
    for(int i = 0; i < USER_SIZE; i++) {
        printf("%d ", accept_fds[i]);
    }
    printf("\n");
}

