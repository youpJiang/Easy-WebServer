#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cassert>
#include <string.h>
#include <sys/epoll.h>
#include<iostream>

#include"./lock/locker.h"
#include"./CGImysql/sql_connection_pool.h"
#include"./threadpool/thread_pool.h"
#include"./http/http_conn.h"
#include"./timer/timer.h"


#define MAX_FD 65536 //max fd count
#define MAX_EVENT_NUMBER 10000
#define TIMESLOT 5 //to trigger SIGALRM

#define listenfdLT //水平触发阻塞

extern int addfd(int epollfd, int fd, bool one_shot);
extern int remove(int epollfd, int fd);
extern int setnonblocking(int fd);

//set timer related args.
static int pipefd[2];
static SortTimerList timerlist;
static int epollfd;

//process SIG.
void sig_handler(int sig)
{
    int save_errno = errno;
    int msg = sig;
    send(pipefd[1], (char *)&msg, 1, 0);
    errno = save_errno;
}
//handle timer task, retime to continuously trigger the SIGALRM.
void timer_handler()
{
    timerlist.Tick();
    alarm(TIMESLOT);
}

//set handler for different signal.
void addsig(int sig, void(handler)(int), bool restart = true)
{
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if (restart)
        sa.sa_flags |= SA_RESTART;
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}

//callback func in timer, delete event in core events table and close fd.
void callback_func(ClientData* userdata)
{
    //delete event
    epoll_ctl(epollfd, EPOLL_CTL_DEL, userdata->sockfd_, 0);
    assert(userdata);

    //close fd.
    close(userdata->sockfd_);
    cout << "删除连接sockfd：" << userdata->sockfd_ << endl;
    HttpConn::m_user_count--;
}

int main(int argc, char* argv[]){
    if (argc <= 1)
    {
        printf("usage: %s ip_address port_number\n", basename(argv[0]));
        return 1;
    }
    int port = atoi(argv[1]);
    addsig(SIGPIPE, SIG_IGN);//当一个进程尝试写入已经关闭的套接字（socket）时，操作系统会向该进程发送SIGPIPE信号
    
    //create sql connection pool.
    ConnectionPool *sqlpool = ConnectionPool::GetInstance();
    sqlpool->Init("localhost", "root", "youpjiang", "webserver_db", 3306, 8);

    //create the threadpool
    ThreadPool<HttpConn> *pool = NULL;
    try
    {
        pool = new ThreadPool<HttpConn>(sqlpool);
    }
    catch(...)
    {
        return 1;
    }
    HttpConn *users = new HttpConn[MAX_FD];
    assert(users);

    users->initmysql_result(sqlpool);
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
    epollfd = epoll_create(1);
    
    if(-1 == epollfd){
        std::cout << "create epollfd error." << std::endl;
        close(listenfd);
        return -1;
    }
    addfd(epollfd, listenfd, false);

    HttpConn::m_epollfd = epollfd;

    if(-1 == socketpair(PF_UNIX, SOCK_STREAM, 0, pipefd)) //pipofd[0] read, [1] write.
    {
        std::cout << "socketpair create error." << std::endl;
        return -1;
    }
    setnonblocking(pipefd[1]);
    addfd(epollfd, pipefd[0], false);

    addsig(SIGALRM, sig_handler, false);
    addsig(SIGTERM, sig_handler, false);
    bool stop_server = false;

    ClientData *userstimer = new ClientData[MAX_FD];
    bool timeout = false;
    alarm(TIMESLOT);

    while(!stop_server){
        epoll_event epoll_events[1024];
        int number = epoll_wait(epollfd, epoll_events, 1024, 1000);
        if(number < 0){
            //signal interrupt
            if(EINTR == errno)continue;
            break;
        }else if(number == 0){
            //time out, go on
            continue;
        }

        for(size_t i = 0; i < number; ++i){
            int sockfd = epoll_events[i].data.fd;
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
                    userstimer[clientfd].address_ = clientaddr;
                    userstimer[clientfd].sockfd_ = sockfd;

                    Timer* timer = new Timer();
                    timer->userdata_ = &userstimer[clientfd];
                    timer->callback_func_ = callback_func;
                    
                    time_t cur = time(NULL);
                    timer->expire_ = cur + 3 * TIMESLOT;
                    userstimer[clientfd].timer_ = timer;
                    timerlist.AddTimer(timer);
            }
            //if event is readable
            else if(epoll_events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                //close the conn and remove timer.
                callback_func(&userstimer[sockfd]);
                Timer* timer = userstimer[sockfd].timer_;
                if(timer)
                    timerlist.DelTimer(timer);
            }
            //recive SIGALRM
            else if((sockfd == pipefd[0]) && (epoll_events[i].events & EPOLLIN))
            {
                char signals[1024];
                int ret = recv(pipefd[0], signals, sizeof(signals), 0);
                if(-1 == ret || 0 == ret)
                {
                    continue;
                }
                else
                {
                    for(int i = 0; i < ret; ++i)
                    {
                        switch(signals[i])
                        {
                        case SIGALRM:
                        {
                            timeout = true;
                            break;
                        }
                        case SIGTERM:
                        {
                            stop_server = true;
                        }
                        }
                    }
                }
                
            }
            else if(epoll_events[i].events & EPOLLIN){
                //listenfd: get the new clientfd
                Timer* timer = userstimer[sockfd].timer_;
                if(users[sockfd].read_once()){
                    pool->append(users + sockfd);
                    std::cout << "client fd: " << sockfd << " calling process func." << std::endl;
                    //timer delayed by 3 units, if data transmission.
                    if(timer)
                    {
                        time_t cur = time(NULL);
                        timer->expire_ = cur + 3 * TIMESLOT;
                        timerlist.AdjTimer(timer);
                    }
                }else{
                    //close the conn and remove timer;
                    callback_func(&userstimer[sockfd]);
                    if(timer)
                    {
                        timerlist.DelTimer(timer);
                    }
                    std::cout << "Error: read_once!——Close conn and remove timer!" << std::endl;
                }
            }
            
            
            else if(epoll_events[i].events & EPOLLOUT){
                Timer* timer = userstimer[sockfd].timer_;
                if(users[sockfd].write()){
                    std::cout << "users[sockfd].write():sent data to brower." << std::endl;
                    if(timer)
                        {
                            time_t cur = time(NULL);
                            timer->expire_ = cur + 3 * TIMESLOT;
                            timerlist.AdjTimer(timer);
                        }
                }
                else 
                {
                    std::cout << "users[sockfd].write(): error.——Close conn and remove timer!" << std::endl;
                    //close the conn and remove timer;
                    callback_func(&userstimer[sockfd]);
                    if(timer)
                    {
                        timerlist.DelTimer(timer);
                    }
                }
            }
        }
        if(timeout)
        {
            timer_handler();
            timeout = false;
        }
        
    }
    close(listenfd);
    close(epollfd);
    close(pipefd[0]);
    close(pipefd[1]);
    delete[] users;
    delete[] userstimer;
    delete pool;
    return 0;
}