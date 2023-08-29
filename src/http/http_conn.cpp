#include "http_conn.h"
#include <cstdio>

#include "logger.h"
using namespace ariesfun::log;

int HttpConn::m_http_epollfd = -1;
int HttpConn::m_client_cnt = 0;

void HttpConn::init(int sockfd, const sockaddr_in &addr)
{
    m_http_sockfd = sockfd; // 保存当前连接的客户端信息
    m_http_addr = addr;
    int reuse = 1; // 设置端口复用
    setsockopt(m_http_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    // proactor模式，先让HttpConn类来处理
    epoller->add_fd(m_http_epollfd, sockfd, true);
    m_client_cnt++;
}

void HttpConn::close_conn() 
{
    if(m_http_sockfd != -1) {
        epoller->remove_fd(m_http_epollfd, m_http_sockfd);
        m_http_sockfd = -1;
        m_client_cnt--;
    }
}

bool HttpConn::read() 
{
    printf("already read all data!\n");
    return true;
}

bool HttpConn::write() 
{
    printf("already write all data!\n");
    return true;
}

// 由线程池中的工作线程调用，这是处理HTTP请求的入口函数
void HttpConn::start_process() 
{
    // 1.解析HTTP请求(进行业务的逻辑处理)
    // 2.生成响应 ()
    // 3.返回数据给客户端

    printf("parse request data, create respondse!\n");
}
