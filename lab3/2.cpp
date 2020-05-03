#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <fcntl.h>
#include <errno.h>
#include <iostream>
#include <vector>
#include <set>
#include <mutex>
#include <condition_variable>
#include <thread>

using namespace std;

#define USER_SIZE 32
#define PROMPT_SIZE 32
#define BUF_SIZE 1024
#define A

int accept_fds[USER_SIZE] = {0};
struct timeval timeout = {0, 0};
int finishes[USER_SIZE];

class Client;
class Server;

class Message {
public:
    string buffer;
    int from;
    Message() : buffer(string()), from(-1) {};
    Message(string buffer, int from) : buffer(buffer), from(from) {};
};

class MessageManager {
public:
    string buffer;
    vector<Message> msgs;

    MessageManager() {
        msgs.clear();
    }

    void feed(string data, int from);
    Message get_one_msg();
};

class Client {
private:
    char prompt[32];
public:
    int fd;
    int returned = false;
    MessageManager tosend;
    MessageManager reader;
    string send_queue;
    Server* server_ptr;
    mutex reader_mutex;
    condition_variable reader_cv;
    thread* recv_thread; 
    thread* send_thread;

    Client(int fd, Server* server_ptr) : fd(fd), server_ptr(server_ptr), returned(false) {};

    inline bool all_joinable();
    inline void all_join();
    void recv_thread_fn();
    void send_thread_fn();
    int run();
    bool alive();
};

class Server {
public:
    set<Client*> client_set;
    int port;
    int max_fd;
    int fd;
    mutex accept_mutex;
    condition_variable accept_cv;

    Server(int port);
    int start();
    void update_client_set();
};

void MessageManager::feed(string data, int from) {
    buffer += data;
    // 按照'\n'分割字符串
    while(true) {
        size_t pos = buffer.find('\n');
        if(pos == string::npos) break;
        char prompt[PROMPT_SIZE];
        if(from != -2) sprintf(prompt, "[Message from %2d] ", from);
        else memset(prompt, 0, PROMPT_SIZE*sizeof(char));
        msgs.push_back(Message(string(prompt) + buffer.substr(0UL, pos + 1), from));
        if(pos + 1 == buffer.length()) buffer = string();
        else buffer = buffer.substr(pos + 1);
    }
}

Message MessageManager::get_one_msg() {
    if(msgs.size() <= 0) return Message();
    Message msg = msgs[0];
    msgs.erase(msgs.begin());
    return msg;
}

inline bool Client::all_joinable() {
    return (recv_thread->joinable() && send_thread->joinable());
}

inline void Client::all_join() {
    recv_thread->join();
    send_thread->join();
    delete recv_thread;
    delete send_thread;
}

void Client::recv_thread_fn() {
    ssize_t recv_size;

    while(true) {
        char buffer[2*BUF_SIZE];
        int i = 0;
        while((recv_size = recv(fd, buffer, BUF_SIZE, 0)) > 0) {
            buffer[recv_size] = '\0';
            if(strlen(buffer) > 1024) {
                FILE* f = fopen("what.txt", "w");
                fprintf(f, "%s", buffer);
                fclose(f);
            }
            reader.feed(string(buffer), -2);
            if(reader.msgs.size() > 0 || !alive()) break;
        }

        for(auto &&clt : server_ptr->client_set) {
            if(clt == this) continue;
            lock_guard<mutex> reader_lock_guard(clt->reader_mutex);
            for(auto msg : reader.msgs) {
                char prompt[PROMPT_SIZE];
                sprintf(prompt, "[Message from %2d] ", fd);
                clt->tosend.msgs.push_back(Message(string(prompt) + msg.buffer, fd));
            }
            clt->reader_cv.notify_all();
        }

        for(auto it = reader.msgs.begin(); it != reader.msgs.end();) {
            it = reader.msgs.erase(it);
        }

        if(!alive()) break;
    }
    // 唤醒线程
    returned = true;
    reader_cv.notify_all();
}

void Client::send_thread_fn() {
    while(true) {
        unique_lock<mutex> reader_lock(reader_mutex);
        Message msg;
        reader_cv.wait(reader_lock, [&]{ return tosend.msgs.size() > 0 || send_queue.length() > 0 || returned; });
        if(returned) break;
        if(send_queue.length() == 0) {
            msg = tosend.get_one_msg();
            send_queue = msg.buffer;
        }
        reader_lock.unlock();
        ssize_t sent_size = send(fd, send_queue.c_str(), send_queue.length() > BUF_SIZE ? BUF_SIZE : send_queue.length(), MSG_NOSIGNAL);
        if(sent_size == -1) continue;
        if(sent_size == send_queue.length()) send_queue = string();
        else send_queue = send_queue.substr(sent_size);
    }
}

int Client::run() {
    recv_thread = new thread(&Client::recv_thread_fn, ref(*this));
    send_thread = new thread(&Client::send_thread_fn, ref(*this));
    return 0;
}

bool Client::alive() {
    if(fd < 0) return 0;
    char temp[32]; 
    ssize_t recv_size = 0;
    recv_size = recv(fd, temp, sizeof(temp), MSG_PEEK | MSG_DONTWAIT);
    
    if(errno == EPIPE || errno == ECONNRESET || recv_size == 0) {
        return false;
    }
    return true;
}

Server::Server(int port) {
    if ((fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket");
        exit(1);
    }

    int on = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(int));
    
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    socklen_t addr_len = sizeof(addr);
    if (bind(fd, (struct sockaddr* )&addr, sizeof(addr))) {
        perror("bind");
        exit(1);
    }
    if (listen(fd, 2)) {
        perror("listen");
        exit(1);
    }
};

int Server::start() {
    thread update_thread = thread(&Server::update_client_set, ref(*this));
    while(1) {
        /* 阻塞等待accept */
        int new_fd = accept(fd, NULL, NULL);
        int on = 1;
        setsockopt(new_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(int));
        char prompt[50];
        sprintf(prompt, "[Server] Connecting...\n");
        send(new_fd, prompt, strlen(prompt), 0);
        /* 加accept锁 */
        unique_lock<mutex> accept_lock(accept_mutex);

        /* 等待accept条件变量 */
        accept_cv.wait(accept_lock, [&]{ return client_set.size() < USER_SIZE; });
        if(new_fd == -1) {
            perror("accept");
            return 1;
        }

        Client* clt = new Client(new_fd, this);
        client_set.insert(clt);

        sprintf(prompt, "[Server] Connect successfully! Your fd is: %d\n", new_fd);
        clt->tosend.feed(string(prompt), -2);
        printf("<<<<<<<<<<<<<<<<<<<<A user(fd: %2d) has connnected! The current user_num: %2ld.\n", new_fd, client_set.size());

        // 已经准备好, 启动客户端线程
        clt->run();
        accept_lock.unlock();
    }
    update_thread.join();
}

void Server::update_client_set() {
    while(true) {
        lock_guard<mutex> accept_lock_guard(accept_mutex);
        for(auto clt_it = client_set.begin(); clt_it != client_set.end();) {
            if((*clt_it)->returned) {
                Client* clt = (Client*)*clt_it;
                clt->all_join();
                int fd = clt->fd;
                shutdown(clt->fd, SHUT_RDWR);
                close(clt->fd);
                printf("User(fd:%2d) has exited! The current user_num: %2ld.>>>>>>>>>>>>>>>>>>>>\n", fd, client_set.size()-1);
                client_set.erase(clt_it++);
                // delete clt; // 对于set, map, list等, erase时, 元素所占内存会自动被释放.
            }
            else clt_it++;
        }
        accept_cv.notify_all();
    }
}


int main(int argc, char **argv) {
    Server* server_ptr = new Server(atoi(argv[1]));
    
    while(true) {
        server_ptr->start();
    }
    return 0;
}