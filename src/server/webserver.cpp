#include "webserver.h"
#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <libgen.h> // basename
#include <cerrno>
#include <csignal>
#include <cassert>

#include "logger.h"
using namespace ariesfun::log;

WebServer::WebServer(int Port)
{
    serverPort = Port;
}

void WebServer::server_init()
{
    // 日志类配置初始化
    Logger::getInstance()->open("../log_info/LOG_INFO.log");
    Logger::getInstance()->log_setlevel(Logger::INFO);
    Logger::getInstance()->log_maxsize(102400);
    Info("-------------INIT-----------------");
    Info("--------SERVER STARTING-----------");
    Info("----------------------------------");

    WebServer::client_info = new HttpConn[MAX_CLIENT_FD];
    memset(events, 0, sizeof(events)); // 将整个数组清零
    t_pool = std::make_unique<ThreadPool<HttpConn>>(8, 10000); // 初始化线程池

    listen_fd = check_error(socket(PF_INET, SOCK_STREAM, 0),
                            "create listen sockfd failed!"); // 创建监听fd

    // 绑定可以连接的用户信息
    struct sockaddr_in sock_address;
    sock_address.sin_family = AF_INET;
    sock_address.sin_addr.s_addr = INADDR_ANY;
    sock_address.sin_port = htons(serverPort);

    int reuse = 1; // 设置端口复用
    check_error(setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)),
                "setsockopt failed!");

    check_error(bind(listen_fd, (struct sockaddr*)&sock_address, sizeof(sock_address)),
                "bind listen sockfd failed!");

    check_error(listen(listen_fd, 5),
                "start listen socket failed!");

    epoll_fd = check_error(epoll_create(1),
                           "epoll_create call failed!");
    // 将监听的fd放入epoll_fd中
    epoller->add_fd(epoll_fd, listen_fd, false);
    HttpConn::m_http_epollfd = epoll_fd;

    // 处理断开事件
    handle_sig(SIGPIPE, SIG_IGN); // 处理关闭连接时的SIGPIPE信号
}

void WebServer::start()
{
    server_init();
    while(true) { // 循环检测是否有连接事件
    int num = epoll_wait(epoll_fd, events, MAX_LISTEN_EVENT_NUM, -1); // 返回检测到的事件数量
    if((num < 0) && (errno != EINTR)) { // 调用失败，并且前一个调用信号中断
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
            if(connfd == -1) {
                Error("socket accept client failed!");
                continue;
            }
            if(HttpConn::m_client_cnt >= MAX_CLIENT_FD) {
                Info("To Client: the current client too much, server is busy!"); // 要返回给客户端
                close(connfd);
                continue;
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
}
}

int WebServer::check_error(int ret, const char* format) // 进行错误检查
{
    if(ret == -1) { // linux系统IO函数失败会返回-1
        Error(format);
    }
    return ret;
}

void WebServer::handle_sig(int sig, void(handler)(int)) // 进行信号捕捉
{
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    sigfillset(&sa.sa_mask); // 设置临时的阻塞信号集
    assert(sigaction(sig, &sa, nullptr) != -1); // 进行信号注册
}

WebServer::~WebServer() 
{
    // 释放资源
    close(epoll_fd);
    close(listen_fd);
    delete[] client_info;
}
