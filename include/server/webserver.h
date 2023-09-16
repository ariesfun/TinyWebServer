#ifndef TINYWEBSERVER_WEBSERVER_H
#define TINYWEBSERVER_WEBSERVER_H

#include <memory>
#include <sys/epoll.h>
#include <string>
#include <vector>
#include "locker.h"
#include "thread_pool.h"
#include "http_conn.h"
#include "epoller.h"

#define MAX_CLIENT_FD 65536 // 可连接的最大客户端数量
#define MAX_LISTEN_EVENT_NUM 10000 // 一次监听时最大的事件数

#include "timer.h"
using namespace ariesfun::timer;

// 服务器模拟的是Proactor异步网络模式
// 在主线程中去监听连接事件，有读事件时取出（给了HttpConn类处理），封装成任务对象，交给线程池中的子线程来处理
class WebServer {
public:
    WebServer(int port, int thread_num, int thread_request_num);
    ~WebServer();

    void log_init();
    void server_init();
    void start();
    
    int check_error(int ret, const char* format); // 错误处理
    void handle_sig(int sig, void(handler)(int)); // 捕获信号

private:
    int listen_fd;
    int epoll_fd;

    std::unique_ptr<ThreadPool<HttpConn>> m_tpool; // 初始化线程池，处理的是http连接任务
    std::unique_ptr<Epoller> m_epoller;
    std::unique_ptr<Timer> m_timer;

    epoll_event ev_res[MAX_LISTEN_EVENT_NUM];      // 创建主线程的epoll事件数组
    HttpConn* client_info;                         // 保存客户端信息的数组
    std::set<int> active_clients;                  // 保存目前活跃的客户端信息
    std::vector<int> sockets_to_remove;

private:
    
    int serverPort;         // 服务器的一些配置信息
    int threadpoolNum;
    int threadpoolRequest;
    // std::string mysqlHost;
    // std::string mysqlUser;
    // std::string mysqlPwd;
    // std::string mysqlDatabase;
};

#endif // TINYWEBSERVER_WEBSERVER_H
