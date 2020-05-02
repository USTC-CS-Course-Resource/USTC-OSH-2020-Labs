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

#define USER_SIZE 2

int accept_fds[USER_SIZE] = {0};
struct timeval timeout = {0, 0};
int finishes[USER_SIZE];
int BUF_SIZE = 16384;

class Message {
public:
    string buffer;
    int from;
    Message() : buffer(string()), from(-1) {};
    Message(string buffer, int from) : buffer(buffer), from(from) {};
};

class MessageReader {
public:
    string buffer;
    vector<Message> msgs;

    MessageReader() {
        msgs.clear();
    }

    void feed(string data, int from) {
        buffer += data;
        // 按照'\n'分割字符串
        while(true) {
            size_t pos = buffer.find('\n');
            if(pos == string::npos) break;
            msgs.push_back(Message(buffer.substr(0UL, pos + 1), from));
            if(pos + 1 == buffer.length()) buffer = string();
            else buffer = buffer.substr(pos + 1);
        }
    }

    Message get_one_msg() {
        if(msgs.size() <= 0) return Message();
        Message msg = msgs[0];
        msgs.erase(msgs.begin());
        return msg;
    }
};

class Client {
private:
    char prompt[32];
public:
    int fd;
    MessageReader reader;
    string send_queue;

    Client(int fd) {
        this->fd = fd;
    }

    string recv_some(fd_set* recv_clients) {
        if(!FD_ISSET(fd, recv_clients)) return string();
        char* buffer = new char[BUF_SIZE+1];
        recv(fd, buffer, BUF_SIZE, 0);
        string string_buffer = string(buffer);
        delete [] buffer;
        return string_buffer;
    }

    void send_some(fd_set* send_clients) {
        if(!FD_ISSET(fd, send_clients)) return; // 不能发送, 返回
        Message msg;
        if(send_queue.length() == 0) {
            msg = reader.get_one_msg();
            send_queue = msg.buffer;
        }
        if(send_queue.length() == 0) return; // 没有可发送消息, 返回
        else {
            sprintf(prompt, "[Message from %2d] ", msg.from);
            send(fd, prompt, strlen(prompt), MSG_NOSIGNAL);
        }
        ssize_t sent_size = send(fd, send_queue.c_str(), send_queue.length(), MSG_NOSIGNAL);
        if(sent_size == -1) return;
        send_queue = send_queue.substr(sent_size);
    }
};

class Server {
public:
    set<Client*> client_set;
    fd_set* clients_fd_set;
    int port;
    int max_fd;
    int fd;

    Server(int port) {
        if ((fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
            perror("socket");
            exit(1);
        }

        int on = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(int));
        int opt_val = 0;
        socklen_t opt_len = sizeof(opt_len);
        getsockopt(fd, SOL_SOCKET, SO_SNDBUF, (char*)&opt_val, &opt_len);
        BUF_SIZE = opt_val;
        getsockopt(fd, SOL_SOCKET, SO_RCVBUF, (char*)&opt_val, &opt_len);
        BUF_SIZE = opt_val > BUF_SIZE ? BUF_SIZE : opt_val;

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
        max_fd = 0;
    };

    ~Server() {
        delete clients_fd_set;
    }

    int accept_one() {
        if(client_set.size() == USER_SIZE) return -1; // 若满, 则不可接受
        int new_fd = accept(fd, NULL, NULL); // 否则接受一个客户端
        if(new_fd < 0) return -1; // 说明此时没有可接受连接的客户端
        fcntl(new_fd, F_SETFL, fcntl(new_fd, F_GETFL, 0) | O_NONBLOCK); // 设置客户端为非阻塞
        Client* new_client_ptr = new Client(new_fd);
        client_set.insert(new_client_ptr);
        
        printf("<<<<<<<<<<<<<<<<<<<<A user(fd: %2d) has connnected! The current user_num: %2ld.\n", new_fd, client_set.size());

        // 向客户端发送连接的信息
        char prompt[50];
        sprintf(prompt, "[Server] Connecting...\n");
        send(new_fd, prompt, strlen(prompt), MSG_NOSIGNAL);
        sprintf(prompt, "[Server] Connect successfully! Your fd is: %2d\n", new_fd);
        send(new_fd, prompt, strlen(prompt), MSG_NOSIGNAL);
    }

    int update_client_set() {
        for(auto clt_it = client_set.begin(); clt_it != client_set.end();) {
            if(update_client(**clt_it) == -1) client_set.erase(clt_it++);
            else clt_it++;
        }
    }

    int update_client(Client &clt) {
        char temp[32]; 
        ssize_t recv_size = 0;
        recv_size = recv(clt.fd, temp, sizeof(temp), MSG_PEEK | MSG_DONTWAIT);
        //printf("[%d] recv_size=%d, errno=%d\n", accept_fds[index], recv_size, errno);
        if(((errno == EPIPE || errno == ECONNRESET || errno == EBADF) && recv_size < 0) || recv_size == 0) {
            int fd = clt.fd;
            close(clt.fd);
            printf("User(fd:%2d) has exited! The current user_num: %2ld.>>>>>>>>>>>>>>>>>>>>\n", fd, client_set.size()-1);
            delete &clt;
            return -1;
        }
        return 0;
    }

    int get_max_fd() {
        int max_fd = 0;
        for(auto &&clt : client_set) {
            if(max_fd < clt->fd) {
                max_fd = clt->fd;
            }
        }
        return max_fd;
    }

    void update_fd_set() {
        FD_ZERO(clients_fd_set);
        for(auto &&clt : client_set) {
            FD_SET(clt->fd, clients_fd_set);
        }
    }

    int recv_all_once() {
        update_fd_set();
        if(select(get_max_fd() + 1, clients_fd_set, NULL, NULL, &timeout) <= 0) return -1; // 没有可读, 返回
        for(auto &&clt : client_set) {
            string recv_buf = clt->recv_some(clients_fd_set);
            // 更新所有其他客户端的发送队列
            for(auto &&clt_tofeed : client_set) {
                if(clt_tofeed == clt) continue;
                clt_tofeed->reader.feed(recv_buf, clt->fd);
            }
        }
    }

    int send_all_once() {
        update_fd_set();
        if(select(get_max_fd() + 1, NULL, clients_fd_set, NULL, &timeout) <= 0) return -1; // 没有可发送客户端, 返回
        for(auto &&clt : client_set) {
            clt->send_some(clients_fd_set);
        }
    }
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
