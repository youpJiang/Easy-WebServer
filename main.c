#include<assert.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<sys/uio.h>
#include<sys/mman.h>
#include<arpa/inet.h>
#include<unistd.h>
#include<fcntl.h>
#include<sys/epoll.h>
#include<poll.h>
#include<iostream>
#include<string.h>
#include<vector>
#include<errno.h>
#include<iostream>
#include<fstream>

#include"./http/http_conn.h"

#define MAX_FD 65536 //max fd count
#define MAX_EVENT_NUMBER 10000

int main(int argc, char* argv[]){
    if (argc <= 1)
    {
        printf("usage: %s ip_address port_number\n", basename(argv[0]));
        return 1;
    }
    int port = atoi(argv[1]);

    HttpConn *users = new HttpConn[MAX_FD];
    assert(users);

    //create listenfd
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if(listenfd == -1){
        std::cout << "listenfd create error." << std::endl;
        return -1;
    }

    //set reuse_IP and reuse_port
    int on = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, (char*)&on, sizeof(on));
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEPORT, (char*)&on, sizeof(on));

    //init server IP
    struct sockaddr_in bindaddr;
    bindaddr.sin_family = AF_INET;
    bindaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    bindaddr.sin_port = htons(port);

    //bind
    if(-1 == bind(listenfd, (struct sockaddr*)&bindaddr, sizeof(bindaddr))){
        std::cout << "bind listen socker error." << std::endl;
        close(listenfd);
        return -1;
    }

    //start listening
    if(-1 == listen(listenfd, SOMAXCONN)){
        std::cout << "listen error." << std::endl;
        close(listenfd);
        return -1;
    }

    //create epollfd
    int epollfd = epoll_create(1);
    
    if(-1 == epollfd){
        std::cout << "create epollfd error." << std::endl;
        close(listenfd);
        return -1;
    }

    
    epoll_event listen_fd_event;
    listen_fd_event.data.fd = listenfd;
    listen_fd_event.events = EPOLLIN;
    listen_fd_event.events |= EPOLLET;

    //bind listenfd to epollfd
    if(epoll_ctl(epollfd, EPOLL_CTL_ADD, listenfd, &listen_fd_event) == -1){
        std::cout << "epoll_ctl error." << std::endl;
        close(listenfd);
        return -1;
    }
    HttpConn::m_epollfd = epollfd;
    int n;
    int count = 0;
    while(true){
        epoll_event epoll_events[1024];
        n = epoll_wait(epollfd, epoll_events, 1024, 1000);
        if(n < 0){
            //signal interrupt
            if(EINTR == errno)continue;
            break;
        }else if(n == 0){
            //time out, go on
            continue;
        }

        for(size_t i = 0; i < n; ++i){
            int sockfd = epoll_events[i].data.fd;
            //if event is readable
            if(epoll_events[i].events & EPOLLIN){
                std::cout << "EPOLLIN:" << sockfd <<std::endl;
                //listenfd: get the new clientfd
                if(sockfd == listenfd){
                    sockaddr_in clientaddr;
                    socklen_t clientaddrlen = sizeof(clientaddr);
                    int clientfd = accept(listenfd, (struct sockaddr*)&clientaddr, &clientaddrlen);
                    if(0 > clientfd){
                        std::cout << "Accept error!" << std::endl;
                        continue;
                    }
                    if(HttpConn::m_user_count >= MAX_FD){
                        std::cout << "Internal server busy!" << std::endl;
                        continue;
                    }
                    users[clientfd].init(clientfd, clientaddr);

                //clientfd: recv data.
                }else{
                    if(users[sockfd].read_once()){
                        users[sockfd].process();
                        std::cout << "client fd: " << sockfd << " calling process func." << std::endl;
                    }else{
                        std::cout << "Error: read_once!" << std::endl;
                    }
                }
            }else if(epoll_events[i].events & EPOLLERR){
            //TODO: ignore the error.
            }else if(epoll_events[i].events & EPOLLOUT){
                if(users[sockfd].write()){
                    std::cout << "sent data to brower." << std::endl;
                }
                else std::cout << "ERROR: sending data to brower." << std::endl;
            }
        }
        
    }
    close(listenfd);
    return 0;
}