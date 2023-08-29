#include "http_conn.h"
#include <cstdio>
#include <sys/epoll.h>

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

    parse_init() 
}

void HttpConn::parse_init() // 初始化解析信息
{
    m_cur_mainstate = CHECK_STATE_REQUESTLINE; // 初始化为解析的请求首行
    m_cur_index = 0;
    m_start_line = 0;
    m_read_index = 0;
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
    // 循环读直到客户端断开
    if(m_read_index >= READ_BUFFER_SIZE) {
        return false;
    }
    // 读到的字节
    int read_bytes = 0;
    while(true) {
        read_bytes = recv(m_http_sockfd, m_readbuffer + m_read_index, READ_BUFFER_SIZE - m_read_index, 0);
        if(read_bytes == -1) {
            if(errno == EAGAIN || errno == EWOULDBLOCK) { // 没有数据了
                break;
            }
            return false;
        }
        else if(read_bytes == 0) { // 连接关闭
            return false;
        }
        m_read_index += read_bytes;
    }
    printf("read data is:\n%s", m_readbuffer);
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
    HTTP_CODE read_ret = parse_request(); // 根据不同状态来处理业务，状态会发生转移
    if(read_ret == NO_REQUEST) { // 请问不完整
        epoller->modify_fd(m_http_epollfd, m_http_sockfd, EPOLLIN);
        return;
    }

    // 2.生成响应 ()
    // 3.返回数据给客户端

    printf("parse request data, create respondse!\n");
}

// 主状态机，解析请求
HttpConn::HTTP_CODE HttpConn::parse_request()
{
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE request_ret = NO_REQUEST; 
    char* line_data = nullptr;
    while((m_cur_mainstate == CHECK_STATE_CONTENT) && (line_status = LINE_OK) ||
          ((line_status = parse_detail_line()) == LINE_OK)) {
        // 解析到了请求体和一行数据
        line_data = get_linedata();
        m_start_line = m_cur_index;
        printf("got 1 http line data：\n%s", line_data);
        switch (m_cur_mainstate)
        {
        case CHECK_STATE_REQUESTLINE:
            /* code */
            break;
        case CHECK_STATE_HEADER:
            /* code */
            break;
        case CHECK_STATE_CONTENT:
            /* code */
            break;            
                    
        default:
            break;
        }

    }

    return NO_REQUEST;
}

HttpConn::HTTP_CODE HttpConn::parse_request_line(char* text)
{
    return NO_REQUEST;
}

HttpConn::HTTP_CODE HttpConn::parse_request_headers(char* text)
{
    return NO_REQUEST;
}

HttpConn::HTTP_CODE HttpConn::parse_request_content(char* text)
{
    return NO_REQUEST;
}
HttpConn::LINE_STATUS HttpConn::parse_detail_line()
{
    return LINE_OK;
}   


char* HttpConn::get_linedata() 
{
    return m_readbuffer + m_start_line;
}