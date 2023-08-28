#ifndef TINYWEBSERVER_HTTP_CONN_H
#define TINYWEBSERVER_HTTP_CONN_H

#include <memory>
#include <netinet/in.h> // sockaddr_in类型
#include "epoller.h"

class HttpConn {
public:
    HttpConn() {}
    ~HttpConn() {}

    void init(int sockfd, const sockaddr_in &addr); // 初始化当前建立连接的客户端信息
    void close_conn();  // 关闭已建立的连接

    bool read(); // 一次性读写数据
    bool write();

    void start_process(); // 开始解析请求报文

    static int m_http_epollfd; // 共享数据，所有socket上的事件都被注册到一个epoll对象中
    static int m_client_cnt; // 当前连接的用户数量

private:
    int m_http_sockfd; // 进行socket通信的fd和地址
    sockaddr_in m_http_addr;

    std::unique_ptr<Epoller> epoller; // epool指针对象
};

#endif //TINYWEBSERVER_HTTP_CONN_H
