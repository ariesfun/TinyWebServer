#ifndef TINYWEBSERVER_HTTP_CONN_H
#define TINYWEBSERVER_HTTP_CONN_H

#include <memory>
#include <netinet/in.h> // sockaddr_in类型
#include <sys/stat.h>   // stat结构体
#include <map>
#include "epoller.h"
#include "mysql_conn.h"
#include "dbconn_pool.h"

class HttpConn {
private:
    // 各种请求状态
    enum METHOD { GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT };                    // HTTP请求方法，这里支持 GET 和 POST
    enum CHECK_STATE { CHECK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER, CHECK_STATE_CONTENT };    // 解析客户端请求时，主状态机的状态
    enum HTTP_CODE   { NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, FORBIDDEN_REQUEST,      // 服务器处理HTTP请求的可能结果，报文解析的结果
                       FILE_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION };
    enum LINE_STATUS { LINE_OK = 0, LINE_BAD, LINE_INCOMPLETE };                                  // 从状态机的三种可能状态，即行的读取状态

public:
    HttpConn();
    ~HttpConn();

    void init(int sockfd, const sockaddr_in &addr, time_t tick); // 初始化当前建立连接的客户端信息
    bool client_isvalid();                                       // 判断当前客户端信息的有效性
    void close_conn();                                           // 关闭已建立的连接
    bool read();                                                 // 一次性读数据
    bool write();                                                // 一次性写数据
    void start_process();                                        // 开始解析请求报文

    static int m_http_epollfd;                                   // 共享数据，所有socket上的事件都被注册到一个epoll对象中
    static int m_client_cnt;                                     // 当前连接的用户数量
    static time_t m_active_time;                                 // 记录用户活跃时间
    static const int READ_BUFFER_SIZE = 2048;                    // 读缓冲区大小  
    static const int WRITE_BUFFER_SIZE = 2048;                   // 写缓冲区大小
    static const int FILEPATH_LEN = 200;                         // 访问的文件名路径的最大长度

private:
    void my_init();                         // 初始化一些需要保存的解析信息或响应信息
    char* get_linedata();                   // 获得一行数据

private:
    int m_http_sockfd;                      // 进行socket通信的fd和地址
    sockaddr_in m_http_addr;
    bool clientinfo_is_valid = false;
    std::unique_ptr<Epoller> m_epoller;     // epool指针对象

    char m_readbuffer[READ_BUFFER_SIZE];
    int m_read_index;                       // 标记读入数据的最后一个字节的下一个位置
    int m_cur_index;                        // 当前正在解析的位置
    int m_start_line;                       // 解析行的起始位置

    CHECK_STATE m_cur_mainstate;            // 主状态机所处状态
    HTTP_CODE parse_request();              // 解析HTTP请求, 处理下面的信息
    HTTP_CODE parse_request_line(char* text);
    HTTP_CODE parse_request_headers(char* text);
    HTTP_CODE parse_request_content(char* text);
    LINE_STATUS parse_detail_line();        // 从状态机，根据换行解析
    HTTP_CODE do_request();                 // 做具体的请求处理, 进行业务处理
    void free_mmap();                       // 释放映射的资源 


    char m_writebuffer[WRITE_BUFFER_SIZE];
    int m_write_index;                      // 每次写数据的首位置，待发送的字节数

    // iovec + writev 可以在一个系统调用中操作多个分散的缓冲区
    // 从而避免了多次系统调用的开销
    struct iovec m_iv[2];                   // 结合writev进行写操作
    int m_iv_cnt;                           // 被写的内存块数量

    bool process_response(HTTP_CODE ret);                       // 进行HTTP响应
    bool add_response_info(const char* format, ...);            // 具体的响应行信息（可变参字符串）
    bool add_response_statline(int status, const char* title);
    void add_response_headers(int content_length);
    bool add_response_content(const char* content);
    bool add_response_connstatus();
    bool add_response_contentlen(int content_length);
    bool add_response_contenttype();
    bool add_response_blankline();

public:
    void init_userdb_info();                                    // 预加载数据库中的用户信息

private:
    DBConnPool* db_pool;
    std::shared_ptr<MySQLConn> conn;
    char m_usrname[128], m_usrpwd[128];
    void parse_detail_postinfo();
    void generateHTMLWithMessage(const char* message);
    bool process_register_request();
    bool process_login_request();

private:
    char* m_url;                                                // 请求的目标文件名
    METHOD m_method;                                            // 请求方法
    char* m_version;                                            // 协议版本
    char* m_hostaddr;                                           // 主机地址
    bool m_conn_status;                                         // http连接状态
    int m_content_length;                                       // http请求体消息的总长度
    char m_real_file[FILEPATH_LEN];                             // 客户端请求访问的文件路径
    struct stat m_file_status;                                  // 请求的目标文件状态
    char* m_file_address;                                       // 目标文件被映射到内存的起始位置

    bool is_post_status;                                        // 当前是否为解析post请求
    char* str_postinfo;                                         // 保存用户的post信息

    int send_bytes;                                             // 已发送的字节数
    int to_send_bytes;                                          // 待发送的字节数

    std::map<std::string, std::string> userdb_info;             // 保存用户信息到map中
};

#endif //TINYWEBSERVER_HTTP_CONN_H
