#ifndef TINYWEBSERVER_HTTP_CONN_H
#define TINYWEBSERVER_HTTP_CONN_H

#include <memory>
#include <netinet/in.h> // sockaddr_in类型
#include "epoller.h"

class HttpConn {
// HTTP处理的状态
private:
    // HTTP请求方法，这里只支持GET
    enum METHOD {GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT};
    
    /*
        解析客户端请求时，主状态机的状态
        CHECK_STATE_REQUESTLINE:当前正在分析请求行
        CHECK_STATE_HEADER:当前正在分析头部字段
        CHECK_STATE_CONTENT:当前正在解析请求体
    */
    enum CHECK_STATE { CHECK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER, CHECK_STATE_CONTENT };
    
    /*
        服务器处理HTTP请求的可能结果，报文解析的结果
        NO_REQUEST          :   请求不完整，需要继续读取客户数据
        GET_REQUEST         :   表示获得了一个完成的客户请求
        BAD_REQUEST         :   表示客户请求语法错误
        NO_RESOURCE         :   表示服务器没有资源
        FORBIDDEN_REQUEST   :   表示客户对资源没有足够的访问权限
        FILE_REQUEST        :   文件请求,获取文件成功
        INTERNAL_ERROR      :   表示服务器内部错误
        CLOSED_CONNECTION   :   表示客户端已经关闭连接了
    */
    enum HTTP_CODE { NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, FORBIDDEN_REQUEST, FILE_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION };
    
    // 从状态机的三种可能状态，即行的读取状态，分别表示
    // 1.读取到一个完整的行 2.行出错 3.行数据尚且不完整
    enum LINE_STATUS { LINE_OK = 0, LINE_BAD, LINE_OPEN };

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

    static const int READ_BUFFER_SIZE = 2048; // 读写缓冲区大小  
    static const int WRITE_BUFFER_SIZE = 2048;

private:
    void parse_init();
    char* get_linedata(); // 获得一行数据

private:
    int m_http_sockfd; // 进行socket通信的fd和地址
    sockaddr_in m_http_addr;

    std::unique_ptr<Epoller> epoller; // epool指针对象

    char m_readbuffer[READ_BUFFER_SIZE];
    int m_read_index; // 标记读入数据的最后一个字节的下一个位置
    int m_cur_index; // 当前正在解析的位置
    int m_start_line; // 解析行的起始位置

    CHECK_STATE m_cur_mainstate; // 主状态机所处状态

    HTTP_CODE parse_request(); // 解析HTTP请求, 处理下面的信息
    HTTP_CODE parse_request_line(char* text);
    HTTP_CODE parse_request_headers(char* text);
    HTTP_CODE parse_request_content(char* text);
    LINE_STATUS parse_detail_line(); // 从状态机，根据换行解析
};

#endif //TINYWEBSERVER_HTTP_CONN_H
