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
#include <iostream>
#include <string>
#include <vector>
#include <set>

using namespace std;

#define USER_SIZE 32
#define PROMPT_SIZE 50

struct timeval timeout = {0, 0};
int BUF_SIZE = 1024;

class Message {
public:
    string buffer;
    int from;
    Message() : buffer(string("")), from(-1) {};
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
    char prompt[PROMPT_SIZE];
public:
    int fd;
    MessageManager tosend;
    MessageManager reader;
    string send_queue;

    Client(int fd) : fd(fd) {};
    void recv_once(fd_set* recv_clients);
    void send_some(fd_set* send_clients);
    bool alive();
};

class Server {
public:
    set<Client*> client_set;
    fd_set* clients_fd_set;
    int port;
    int fd;

    Server(int port);

    ~Server() {
        delete clients_fd_set;
    }

    int accept_one();
    void update_client_set();
    int update_client(Client &clt);
    int get_max_fd();
    void update_fd_set();
    int recv_all_once();
    int send_all_once();
};

Server* server_ptr;

int main(int argc, char **argv) {
    server_ptr = new Server(atoi(argv[1]));
    
    while(true) {
        server_ptr->accept_one();
        server_ptr->recv_all_once();
        server_ptr->send_all_once();
        server_ptr->update_client_set();
    }
    return 0;
}

void MessageManager::feed(string data, int from) {
    if(data.empty()) return;
    buffer += data;
    // 按照'\n'分割字符串
    while(true) {
        size_t pos = buffer.find('\n');
        if(pos == string::npos) break;
        char prompt[PROMPT_SIZE];
        if(from != -2) sprintf(prompt, "[Message from %2d] ", from);
        else memset(prompt, 0, PROMPT_SIZE*sizeof(char));
        msgs.push_back(Message(string(prompt) + buffer.substr(0UL, pos) + "\n", from));
        if(pos + 1 == buffer.length()) buffer.erase(buffer.begin(), buffer.end());
        else buffer = buffer.substr(pos + 1);
    }
}

Message MessageManager::get_one_msg() {
    if(msgs.size() <= 0) return Message();
    Message msg = msgs[0];
    msgs.erase(msgs.begin());
    return msg;
}

void Client::recv_once(fd_set* recv_clients) {
    if(!FD_ISSET(fd, recv_clients)) return;
    char buffer[BUF_SIZE + 1];
    memset(buffer, 0, sizeof(char)*(BUF_SIZE + 1));
    ssize_t recv_size;
    recv_size = recv(fd, buffer, BUF_SIZE, MSG_DONTWAIT);
    if(recv_size <= 0) return;
    reader.feed(string(buffer), -2);
    return;
}

void Client::send_some(fd_set* send_clients) {
    if(!FD_ISSET(fd, send_clients)) return; // 不能发送, 返回
    if(send_queue.empty()) {
        Message msg = tosend.get_one_msg();
        send_queue = msg.buffer;
    }
    if(send_queue.empty()) return; // 没有可发送消息, 返回
    ssize_t sent_size = send(fd, send_queue.c_str(), send_queue.length() > BUF_SIZE ? BUF_SIZE : send_queue.length(), MSG_NOSIGNAL);
    if(sent_size < 0) return;
    send_queue = send_queue.substr(sent_size);
}

bool Client::alive() {
    if(fd < 0) return 0;
    char temp[32]; 
    ssize_t recv_size = 0;
    recv_size = recv(fd, temp, sizeof(temp), MSG_PEEK | MSG_DONTWAIT);
    if(errno == EPIPE || errno == ECONNRESET || errno == EAGAIN) {
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

    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK); // 设置服务器fd为非阻塞
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

    clients_fd_set = new fd_set;
};


int Server::accept_one() {
    if(client_set.size() == USER_SIZE) return -1; // 若满, 则不可接受
    int new_fd = accept(fd, NULL, NULL); // 否则接受一个客户端
    if(new_fd < 0) return -1; // 说明此时没有可接受连接的客户端
    fcntl(new_fd, F_SETFL, fcntl(new_fd, F_GETFL, 0) | O_NONBLOCK); // 设置客户端为非阻塞
    Client* new_client_ptr = new Client(new_fd);
    client_set.insert(new_client_ptr);
    
    printf("<<<<<<<<<<<<<<<<<<<<A user(fd: %2d) has connnected! The current user_num: %2ld.\n", new_fd, client_set.size());

    // 向客户端发送连接的信息
    new_client_ptr->tosend.feed("[Server] Connecting...\n", -2);
    char prompt[PROMPT_SIZE];
    sprintf(prompt, "[Server] Connect successfully! Your fd is: %2d\n", new_fd);
    new_client_ptr->tosend.feed(string(prompt), -2);
}

void Server::update_client_set() {
    for(auto clt_it = client_set.begin(); clt_it != client_set.end();) {
        if(update_client(**clt_it) == -1) client_set.erase(clt_it++);
        else clt_it++;
    }
}

int Server::update_client(Client &clt) {
    char temp[32]; 
    ssize_t recv_size = 0;
    recv_size = recv(clt.fd, temp, sizeof(temp), MSG_PEEK | MSG_DONTWAIT);
    if(((errno == EPIPE || errno == ECONNRESET || errno == EBADF) && recv_size < 0) || recv_size == 0) {
        int fd = clt.fd;
        close(clt.fd);
        printf("User(fd:%2d) has exited! The current user_num: %2ld.>>>>>>>>>>>>>>>>>>>>\n", fd, client_set.size()-1);
        delete &clt;
        return -1;
    }
    return 0;
}

int Server::get_max_fd() {
    int max_fd = 0;
    for(auto &&clt : client_set) {
        if(max_fd < clt->fd) {
            max_fd = clt->fd;
        }
    }
    return max_fd;
}

void Server::update_fd_set() {
    FD_ZERO(clients_fd_set);
    for(auto &&clt : client_set) {
        FD_SET(clt->fd, clients_fd_set);
    }
}

int Server::recv_all_once() {
    update_fd_set();
    if(select(get_max_fd() + 1, clients_fd_set, NULL, NULL, &timeout) <= 0) return -1; // 没有可读, 返回
    for(auto &&clt : client_set) {
        clt->recv_once(clients_fd_set);
        // 更新所有其他客户端的发送队列
        for(auto &&other_clt : server_ptr->client_set) {
            if(other_clt == clt) continue;
            for(auto msg : clt->reader.msgs) {
                char prompt[PROMPT_SIZE];
                sprintf(prompt, "[Message from %2d] ", clt->fd);
                other_clt->tosend.msgs.push_back(Message(string(prompt) + msg.buffer, clt->fd));
            }
        }
        for(auto it = clt->reader.msgs.begin(); it != clt->reader.msgs.end();) {
            it = clt->reader.msgs.erase(it);
        }
    }
}

int Server::send_all_once() {
    update_fd_set();
    if(select(get_max_fd() + 1, NULL, clients_fd_set, NULL, &timeout) <= 0) return -1; // 没有可发送客户端, 返回
    for(auto &&clt : client_set) {
        clt->send_some(clients_fd_set);
    }
}
