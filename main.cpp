#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <libgen.h> // basename
#include <cerrno>
#include <csignal>
#include <cassert>

#include <memory>

#include "locker.h"
#include "thread_pool.h"
#include "http_conn.h"
#include "epoller.h"

#define MAX_CLIENT_FD 65535 // 可连接的最大客户端数量
#define MAX_LISTEN_EVENT_NUM 10000 // 一次监听时最大的事件数

#include "logger.h"
using namespace ariesfun::log;


void handle_sig(int sig, void(handler)(int)) // 进行信号捕捉
{
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    sigfillset(&sa.sa_mask); // 设置临时的阻塞信号集
    assert(sigaction(sig, &sa, nullptr) != -1); // 进行信号注册
}

int check_error(int ret, char* msg) // 进行错误检查
{
    if(ret == -1) { // linux系统IO函数失败会返回-1
        Error(msg);
        exit(-1);
    }
    return ret;
}

int main()
{
    // 日志配置
    Logger::getInstance()->open("../log_info/LOG_INFO.log");
    Logger::getInstance()->log_setlevel(Logger::INFO);
    Logger::getInstance()->log_maxsize(2000);


    // 使用宏定义打印日志信息
    Debug("hello world");
    Debug("name is %s, age is %d", "jack", 18);
    Info("info message");
    Warn("warning message");
    Error("error message");
    Fatal("fatal massgae");

    // TODO, 可以放入server模块
    // Server的处理部分
    // 服务器模拟的是Proactor异步网络模式
    // 在主线程中去监听连接事件，有读事件时取出（给了HttpConn类处理），封装成任务对象，交给线程池中的子线程来处理

    // 初始化线程池，处理的是http连接任务
    ThreadPool<HttpConn>* t_pool = nullptr;
    try{
        t_pool = new ThreadPool<HttpConn>;
    } catch(...) {
        exit(-1);
    }

    int listen_fd = check_error(socket(PF_INET, SOCK_STREAM, 0),
                                "create listen sockfd failed!"); // 创建监听fd
    int reuse = 1; // 设置端口复用
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    // 绑定可以连接的用户信息
    struct sockaddr_in sock_address;
    sock_address.sin_family = AF_INET;
    sock_address.sin_addr.s_addr = INADDR_ANY;
    sock_address.sin_port = htons(8888);
    check_error(bind(listen_fd, (struct sockaddr*)&sock_address, sizeof(sock_address)),
                "bind listen sockfd failed!");
    check_error(listen(listen_fd, 5),
                "start listen socket failed!");
    
    // 创建主线程的epoll事件数组
    epoll_event events[MAX_LISTEN_EVENT_NUM];
    int epoll_fd = check_error(epoll_create(5), 
                               "epoll_create call failed");
    // 将监听的fd放入epoll_fd中
    // TODO Epoller方法写法待优化
    std::unique_ptr<Epoller> epoller;
    epoller->add_fd(epoll_fd, listen_fd, false);
    HttpConn::m_http_epollfd = epoll_fd;
    HttpConn* client_info = new HttpConn[MAX_CLIENT_FD]; // 保存客户端信息的数组

    while(true) { // 循环检测是否有连接事件
        int num = epoll_wait(epoll_fd, events, MAX_LISTEN_EVENT_NUM, -1); // 返回检测到的事件数量
        if(num < 0 && (errno != EINTR)) { // 调用失败，并且前一个调用信号终端
            Error("epoll_wait call failed!");
            break;
        }
        // 遍历事件数组
        for(int i = 0; i < num; i++) {
            int sockfd = events[i].data.fd; // 表示该事件与哪个文件描述符相关联
            if(sockfd == listen_fd) { // 当前的sockfd是监听到的fd, 表示有客户端连接进来了
                struct sockaddr_in client_addr;
                socklen_t client_addr_len = sizeof(client_addr); // 转换类型
                int connfd = accept(listen_fd, (struct sockaddr*)&client_addr, &client_addr_len); // 建立连接
                if(connfd < 0) {
                    Error("socket accept client failed!");
                    continue;
                }
                if(HttpConn::m_client_cnt >= MAX_CLIENT_FD) {
                    Info("To Client: the current client too much, server is busy!"); // 要返回给客户端
                }
                client_info[connfd].init(connfd, client_addr);
            }
            else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) { // 对方异常断开或者错误等事件
                client_info[sockfd].close_conn(); // 关闭连接
            }
            else if(events[i].events & EPOLLIN) { // 判断是否有读事件发生，一次性读完数据
                if(client_info[sockfd].read()) {
                    t_pool->add_task(client_info + sockfd); // 传入客户端事件地址
                }
                else {
                    client_info[sockfd].close_conn();
                }
            }   
            else if(events[i].events & EPOLLOUT) { // 一次性写完数据
                if(!client_info[sockfd].write()) {
                    client_info[sockfd].close_conn();
                }
            }
        }
        // 处理断开事件
        handle_sig(SIGPIPE, SIG_IGN); // 处理关闭连接时的SIGPIPE信号
    }

    // 释放资源
    close(listen_fd);
    close(epoll_fd);
    delete[] client_info;
    delete t_pool;
    return 0;
}
