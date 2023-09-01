#ifndef TINYWEBSERVER_WEBSERVER_H
#define TINYWEBSERVER_WEBSERVER_H

#include <memory>
#include <sys/epoll.h>
#include "locker.h"
#include "thread_pool.h"
#include "http_conn.h"
#include "epoller.h"

#define MAX_CLIENT_FD 65536 // 可连接的最大客户端数量
#define MAX_LISTEN_EVENT_NUM 10000 // 一次监听时最大的事件数

// Server的处理部分
// 服务器模拟的是Proactor异步网络模式
// 在主线程中去监听连接事件，有读事件时取出（给了HttpConn类处理），封装成任务对象，交给线程池中的子线程来处理

class WebServer {
public:
    WebServer(int Port);
    ~WebServer();

    void log_init();
    void server_init();
    void start();
    
    // 错误处理
    int check_error(int ret, const char* format);
    void handle_sig(int sig, void(handler)(int));
private:
    int serverPort;

private:
    int listen_fd;
    int epoll_fd;

    std::unique_ptr<ThreadPool<HttpConn>> t_pool; // 初始化线程池，处理的是http连接任务
    std::unique_ptr<Epoller> epoller;

    epoll_event events[MAX_LISTEN_EVENT_NUM]; // 创建主线程的epoll事件数组
    HttpConn* client_info; // 保存客户端信息的数组
};

#endif // TINYWEBSERVER_WEBSERVER_H
