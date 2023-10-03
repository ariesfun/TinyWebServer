#include "http_conn.h"
#include <iostream>
#include <cstring>
#include <sys/epoll.h>
#include <fcntl.h>      // 文件操作
#include <unistd.h>     // close()
#include <sys/mman.h>   // mmap()
#include <sys/uio.h>    // writev()
#include <cstdarg>      // va_start()

#include <jsoncpp/json/json.h>
#include <sstream>

#include "logger.h"
using namespace ariesfun::log;

int HttpConn::m_http_epollfd = -1;
int HttpConn::m_client_cnt = 0;
time_t HttpConn::m_active_time = 0;
std::string HttpConn::chat_reply_str = "";
int HttpConn::buffer_offset = 0;                                  
char HttpConn::chat_reply_buffer[CHAT_BUFFER_SIZE] = {0};

// OPENAI-KEY
const char *openai_key = "sk-ShXXXXXXXXXXXXXXXXXXXXXXXXXXXX";
// 网站资源根目录
const char* web_root_src = "/home/ariesfun/Resume_Projects/TinyWebServer/resources";

// 生产HTTP响应的一些状态信息
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "404 error!\nThe requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the requested file.\n";

HttpConn::HttpConn() {}

HttpConn::~HttpConn() {}

// 初始化客户端的连接状态信息
void HttpConn::init(int sockfd, const sockaddr_in &addr, time_t tick)
{
    m_http_sockfd = sockfd;                             // 保存当前连接的客户端信息
    m_http_addr = addr;
    m_active_time = tick;
    clientinfo_is_valid = true;
    int reuse = 1;                                      // 设置端口复用
    int ret = setsockopt(m_http_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    if(ret == -1) {
        Error("setsockopt oprate error!");
    }
    m_epoller->add_fd(m_http_epollfd, sockfd, true);    // proactor模式，先让HttpConn类来处理
    m_client_cnt++;
    my_init();
    init_userdb_info();                                 // 有用户连接时，预先获取数据中的用户信息 
}

// 初始化解析或响应要保存的信息
void HttpConn::my_init()                        
{
    m_cur_mainstate = CHECK_STATE_REQUESTLINE;  // 初始化为解析的请求行
    m_cur_index = 0;
    m_start_line = 0;
    m_read_index = 0;

    m_url = nullptr;                            // 初始化将要保存的HTTP请求信息
    m_method = GET;
    m_version = nullptr;
    m_hostaddr = nullptr;
    m_conn_status = false;
    m_content_length = 0;

    m_write_index = 0;
    
    send_bytes = 0;
    to_send_bytes = 0;

    is_post_status = false;
    str_postinfo = nullptr;

    bzero(m_readbuffer, READ_BUFFER_SIZE);
    bzero(m_writebuffer, WRITE_BUFFER_SIZE);
    bzero(m_real_file, FILEPATH_LEN);
}

void HttpConn::init_userdb_info()
{
    db_pool = DBConnPool::getDBConnPool();
    conn = db_pool->getDBConn();                            // 从数据库获得一个连接，可以实现连接的回收
    std::string sql = "select username, password from user";
    bool flag = conn->selectDB(sql);
    if(!flag) {
        Error("Failed to preload user information in the database! Select error!");
    }
    while (conn->processQueryResults()) { 
        std::string user = conn->getFieldValue(0);
        std::string pwd = conn->getFieldValue(1);
        userdb_info[user] = pwd;                            // 将查询到的结果保存
    }
}

bool HttpConn::client_isvalid() 
{
    return clientinfo_is_valid;
}

void HttpConn::close_conn() 
{
    if(m_http_sockfd != -1) {
        m_epoller->remove_fd(m_http_epollfd, m_http_sockfd);
        clientinfo_is_valid = false;
        m_http_sockfd = -1;
        m_client_cnt--;
    }
}

// 将数据读入到读缓冲区，解析
bool HttpConn::read()                       
{
    if(m_read_index >= READ_BUFFER_SIZE) {  // 循环读直到客户端断开
        return false;
    }
    int read_bytes = 0;                     // 读到的字节
    while(true) {
        // 从(m_read_buf + m_read_idx)索引出开始保存数据，大小是(READ_BUFFER_SIZE - m_read_idx)
        read_bytes = recv(m_http_sockfd, m_readbuffer + m_read_index, READ_BUFFER_SIZE - m_read_index, 0);
        if(read_bytes == -1) {              // 没有数据要读时
            if(errno == EAGAIN || errno == EWOULDBLOCK) { 
                break;
            }
            return false;
        }
        else if(read_bytes == 0) {          // 对方连接关闭
            return false;
        }
        m_read_index += read_bytes;
    }
    Info("read data is:\n%s", m_readbuffer);
    return true;
}

// 将响应一次性存入到写缓冲区，发送
bool HttpConn::write()          
{
    int temp = 0;
    if(to_send_bytes == 0) {    // 没有要发送的数据了，结束响应
        m_epoller->modify_fd(m_http_epollfd, m_http_sockfd, EPOLLIN);
        my_init();
        return true;
    }
    while(true) { 
        // 进行分散写，iovec用于执行读取和写入操作
        // writev函数将多块数据一并写入套接字，避免了多次数据复制和多次系统调用的开销
        temp = writev(m_http_sockfd, m_iv, m_iv_cnt);
        if(temp == -1) {
            // 若TCP缓冲没有空间，则等待下一轮EPOLLOUT事件
            // 此时虽无法立即收到该客户端的下一个请求，但可以保证连接的稳定性
            if(errno == EAGAIN) { // 修改epoll监听事件来等待下一次写事件
                m_epoller->modify_fd(m_http_epollfd, m_http_sockfd, EPOLLOUT);
                return true;
            }
            free_mmap();
            return false;
        }
        send_bytes += temp;         // 已经发送了多少字节
        to_send_bytes -= temp;      // 更新信息，还剩下多少字节需要发送
        if(send_bytes >= m_iv[0].iov_len) {
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + (send_bytes - m_write_index); // 更新iovec指针的位置
            m_iv[1].iov_len = to_send_bytes;
        }
        else {
            m_iv[0].iov_base = m_writebuffer + send_bytes;
            m_iv[0].iov_len = m_iv[0].iov_len - temp;
        }
        if(to_send_bytes <= 0) {    // 数据已经发送完毕
            free_mmap();            // 重置epolloneshot事件
            m_epoller->modify_fd(m_http_epollfd, m_http_sockfd, EPOLLIN);
            if(m_conn_status) {     // 请求为长连接
                my_init();
                return true;
            }
            else {
                return false;
            }
        }
    }
}

// 由线程池中的工作线程调用，这是处理HTTP请求的入口函数
void HttpConn::start_process() 
{
    // 01.解析HTTP请求，读取数据(进行业务的逻辑处理)
    HTTP_CODE read_ret = parse_request();           // 根据不同状态来处理业务，状态会发生转移
    if(read_ret == NO_REQUEST) {                    // 请问不完整，需要继续接收请求，注册并监听读事件
        m_epoller->modify_fd(m_http_epollfd, m_http_sockfd, EPOLLIN);
        return;
    }
    // 02.生成响应，写数据发给客户端
    bool write_ret = process_response(read_ret);    // 根据请求信息，处理对应的响应信息
    if(!write_ret) {
        close_conn();
    }
    // 03.返回数据给客户端，注册并监听写事件
    m_epoller->modify_fd(m_http_epollfd, m_http_sockfd, EPOLLOUT);
}

// 主状态机根据解析内容变化(有限状态机)，解析请求主要是读数据操作
HttpConn::HTTP_CODE HttpConn::parse_request() 
{
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE request_ret = NO_REQUEST; 
    char* line_data = nullptr;
    while(((m_cur_mainstate == CHECK_STATE_CONTENT) && (line_status == LINE_OK)) || // 主状态机解析请求体，并且从状态机解析行成功作为循环入口
          ((line_status = parse_detail_line()) == LINE_OK)) {                       // 每次解析一行数据 (解析到了请求体或一行完整数据)
        line_data = get_linedata();                                                 // 获得一行数据
        m_start_line = m_cur_index;                                                 // 更新解析的行首位置
        // Info("got 1 http line data:\n%s", line_data);
        switch(m_cur_mainstate)                                                     // 主状态机 (有效状态机)
        {
            case CHECK_STATE_REQUESTLINE:
            {
                request_ret = parse_request_line(line_data);
                if(request_ret == BAD_REQUEST) {                                    // 语法错误
                    return BAD_REQUEST;
                }
                break;
            }     
            case CHECK_STATE_HEADER:
            {   
                request_ret = parse_request_headers(line_data);
                if(request_ret == BAD_REQUEST) {
                    return BAD_REQUEST;
                }
                else if(request_ret == GET_REQUEST) {                               // 获得了一个完整的客户请求
                    return do_request();                                            // 进行响应
                }
                break;
            }
            case CHECK_STATE_CONTENT:                                               // 用于POST，解析请求体的内容
            {
                request_ret = parse_request_content(line_data);
                if(request_ret == GET_REQUEST) {
                    return do_request();
                }
                line_status = LINE_INCOMPLETE;                                      // 解析完成需要退出，更新状态避免再次进行循环，更新行状态为行不完整
                break;
            }          
            default:
                return INTERNAL_ERROR;
        }
    }
    return NO_REQUEST;
}

// 读信息，解析请求首行，提取信息'GET /index.html HTTP/1.1' : 请求方法 | 目标URL | HTTP版本
HttpConn::HTTP_CODE HttpConn::parse_request_line(char* text)
{
    m_url = strpbrk(text, " \t");            // 判断字符空格或制表符哪个先在text中出现，并返回位置
    if(!m_url) {
        return BAD_REQUEST;
    }
    *m_url++ = '\0';
    // GET\0/index.html HTTP/1.1

    char* method = text;                    // 得到请求方法
    if(strcasecmp(method, "GET") == 0) {
        m_method = GET; 
    }
    else if(strcasecmp(method, "POST") == 0) {
        m_method = POST;
        is_post_status = true;
    }
    else {
        return BAD_REQUEST;
    }

    // m_url: /index.html HTTP/1.1
    m_version = strpbrk(m_url, " \t");
    if(!m_version) {
        return BAD_REQUEST;
    }
    *m_version++ = '\0';
    // m_url: /index.html\0HTTP/1.1
    if(strcasecmp(m_version, "HTTP/1.1") != 0) {
        return BAD_REQUEST;
    }

    // http://192.168.1.1:8888/index.html
    if(strncasecmp(m_url, "http://", 7) == 0) {
        m_url += 7; // 192.168.1.1:8888/index.html
        m_url = strchr(m_url, '/');         // /index.html
    }

    if(!m_url || m_url[0] != '/') {
        return BAD_REQUEST;
    }

    if(strlen(m_url) == 1) {                // 当url为'/'时，先显示登录界面
        strcat(m_url, "login.html");
    }

    m_cur_mainstate = CHECK_STATE_HEADER;   // 更新检查状态,主状态机状态变为检查请求头
    return NO_REQUEST;
}

// 解析请求头
HttpConn::HTTP_CODE HttpConn::parse_request_headers(char* text)
{
    if(text[0] == '\0') {                                    // 为空时，头部解析完成
        if(m_content_length != 0) {                          // 如果消息体有内容即为POST请求，状态机状态需要转移
            m_cur_mainstate = CHECK_STATE_CONTENT;
            return NO_REQUEST;                               // 请求不完整，需要继续读取用户数据
        }
        return GET_REQUEST;                                  // 得到了一个完整的请求头部信息
    }
    else if(strncasecmp(text, "Host:", 5) == 0) {            // 主机域名
        text += 5;
        // 192.168.184.10:8888, 此时指针指向'192'的'1'位置
        text += strspn(text, " \t");                         // 移动指针到第一个不在这个字符集合中的字符处
        m_hostaddr = text;
    }
    else if(strncasecmp(text, "Connection:", 11) == 0) {     // 获取连接状态
        text += 11;
        text += strspn(text, " \t");
        if(strcasecmp(text, "keep-alive") == 0) {
            m_conn_status = true;                            // 保持连接
        }
    }
    else if(strncasecmp(text, "Content-Length:", 15) == 0) { // 内容长度
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text);
    }
    else {                                                   // 其他，出现未知头部字段
        // Info("unknow header info:\n%s", text);
    }
    return NO_REQUEST;
}

// 解析请求体内容, POST请求
HttpConn::HTTP_CODE HttpConn::parse_request_content(char* text)
{
    if(m_read_index >= (m_content_length + m_cur_index)) { // 指针能移动到请求体部分，说明可以拿到这部分信息
        text[m_content_length] = '\0';
        str_postinfo = text;
        Info("parse got 1 post information:\n%s", str_postinfo);
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

// 从状态机处理
HttpConn::LINE_STATUS HttpConn::parse_detail_line()     // 解析一行数据报文，判断\r\n
{
    char ch;
    for(; m_cur_index < m_read_index; ++m_cur_index) {
        ch = m_readbuffer[m_cur_index];
        if(ch == '\r') {
            if((m_cur_index + 1) == m_read_index) {     // 是下一次要读的位置
                return LINE_INCOMPLETE;                 // 行不完整
            }
            else if(m_readbuffer[m_cur_index+1] == '\n') {
                m_readbuffer[m_cur_index++] = '\0';     // '\r'置空
                m_readbuffer[m_cur_index++] = '\0';     // '\n'置空
                return LINE_OK;                         // 此时读指针移到了下一行开头
            }
            return LINE_BAD;                            // 行出错
        }
        else if(ch == '\n') {                           // 已经到行尾了
            if((m_cur_index > 1) && (m_readbuffer[m_cur_index-1] == '\r')) {
                m_readbuffer[m_cur_index-1] = '\0';
                m_readbuffer[m_cur_index++] = '\0';     // 当前位置置空后后移
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_INCOMPLETE;                             // 数据不完整，需要继续接收请求
}   

// 当得到一个完整正确的HTTP请求时，我们就分析目标文件的属性
// 如果目标文件（非目录）存在、且是对所有用户可读的状态，则使用mmap将文件映射到内存地址m_file_adress处，并告诉调用者获取文件成功
HttpConn::HTTP_CODE HttpConn::do_request()
{
    strcpy(m_real_file, web_root_src);
    int len = strlen(web_root_src);

    Info("the current request m_url: %s", m_url);  
    if(is_post_status && (strcmp(m_url, "/register") == 0)) {     // 处理注册请求
        bool register_ret = process_register_request();
        if (register_ret) {         
            strncat(m_real_file, "/success.html", FILEPATH_LEN - len - 1);
            // 给客户端生成响应
            // 注册成功，弹窗提示（"注册成功，请先登录"）
        } else {
            // 注册失败，用户名重复
            strncat(m_real_file, "/error.html", FILEPATH_LEN - len - 1);
        }
    }
    else if(is_post_status && (strcmp(m_url, "/login") == 0)) {   // 处理登录请求
        bool login_ret = process_login_request();
        if (login_ret) {
            strncat(m_real_file, "/index.html", FILEPATH_LEN - len - 1);
        } else {
            // 登录失败，用户名或密码错误!
            strncat(m_real_file, "/error.html", FILEPATH_LEN - len - 1);
        }
    }
    else if(is_post_status && (strcmp(m_url, "/send_msg") == 0)) {   // 处理登录请求
        process_chatQA_request();
        return JSON_REQUEST;
    }
    else { // 其他文件请求路径
        if(len < FILEPATH_LEN-1) {
            strncat(m_real_file, m_url, FILEPATH_LEN - len - 1);  // 追加信息，拼接成要访问本地的文件路径
        }
        Info("the client request file path: %s", m_real_file);
    }

    // 请求错误检测
    // 先获取m_real_file文件相关的状态信息，-1失败，0成功
    if(stat(m_real_file, &m_file_status) < 0) {
        return NO_RESOURCE;
    }

    // 判断文件能否访问
    if(!(m_file_status.st_mode & S_IROTH)) {
        return FORBIDDEN_REQUEST;
    }
    // 判断是否是目录
    if(S_ISDIR(m_file_status.st_mode)) {
        return BAD_REQUEST;
    }

    // 以只读方式打开文件
    int file_fd = open(m_real_file, O_RDONLY);

    // 创建内存映射，减少read,write的开销
    // mmap()系统调用使得进程之间通过映射同一个普通文件实现共享内存
    m_file_address = (char*)mmap(0, m_file_status.st_size, PROT_READ, MAP_PRIVATE, file_fd, 0);
    close(file_fd);
    return FILE_REQUEST;
}

// munmap()该调用在进程地址空间中解除一个映射关系, 释放映射的资源
void HttpConn::free_mmap() 
{
    if(m_file_address) {
        munmap(m_file_address, m_file_status.st_size);
        m_file_address = nullptr;
    }
}

char* HttpConn::get_linedata() 
{
    return m_readbuffer + m_start_line;
}

// 根据处理的HTTP请求结果，生成响应发送给客户端
bool HttpConn::process_response(HTTP_CODE ret)
{
    switch (ret)                // 生存对于的HTTP响应报文
    {
        case BAD_REQUEST:       // 语法错误
        {
            add_response_statline(400, error_400_title);
            add_response_headers(strlen(error_400_form), "text/html");
            if(!add_response_content(error_400_form)) {
                return false;
            }
            break;
        }
        case FORBIDDEN_REQUEST: // 访问禁止
        {
            add_response_statline(403, error_403_title);
            add_response_headers(strlen(error_403_form), "text/html");
            if(!add_response_content(error_403_form)) {
                return false;
            }
            break;
        }
        case NO_RESOURCE:       // 没有该资源(404)
        {
            add_response_statline(404, error_404_title);
            add_response_headers(strlen(error_404_form), "text/html");
            if(!add_response_content(error_404_form)) {
                return false;
            }
            break;
        }
        case INTERNAL_ERROR:    // 内部错误
        {   
            add_response_statline(500, error_500_title);
            add_response_headers(strlen(error_500_form), "text/html");
            if(!add_response_content(error_500_form)) {
                return false;
            }
            break;
        }
        case FILE_REQUEST:      // 访问文件请求
        {
            add_response_statline(200, ok_200_title);
            add_response_headers(m_file_status.st_size, "text/html");

            // iovec指向响应报文缓冲区
            m_iv[0].iov_base = m_writebuffer;   // 指向数据块的起始地址
            m_iv[0].iov_len = m_write_index;    // 数据块的长度

            // iovec指向mmap返回的文件指针
            m_iv[1].iov_base = m_file_address;
            m_iv[1].iov_len = m_file_status.st_size;
            m_iv_cnt = 2;

            to_send_bytes = m_write_index + m_file_status.st_size;
            return true;
        }
        case JSON_REQUEST:      // 返回 JSON 数据
        {
            // 创建响应头部
            add_response_statline(200, ok_200_title);
            add_response_headers(reply_content_json.length(), "application/json");
            if(!add_response_content(reply_content_json.c_str())) {
                return false;
            }
            
            // 将 HTTP 头信息写入发送缓冲区
            m_iv[0].iov_base = m_writebuffer;
            m_iv[0].iov_len = m_write_index;

            // 设置要发送的 JSON 数据
            m_iv[1].iov_base = const_cast<char*>(reply_content_json.c_str());
            m_iv[1].iov_len = reply_content_json.size();
            m_iv_cnt = 2;

            // 设置要发送的字节数
            to_send_bytes = m_write_index + reply_content_json.size();
            return true;
        }
        default:
            return false;
    } 
    m_iv[0].iov_base = m_writebuffer; // 默认只存写缓冲区的信息
    m_iv[0].iov_len = m_write_index; 
    m_iv_cnt = 1;
    to_send_bytes = m_write_index;
    return true;
}

// 每行的响应信息格式化封装
bool HttpConn::add_response_info(const char* format, ...)
{
    if(m_write_index >= WRITE_BUFFER_SIZE) {
        return false;
    }
    va_list arg_list;
    va_start(arg_list, format);                          // 存入写缓冲区中
    // vsnprintf(arg_content, len + 1, format, arg_ptr); // 带参数列表的格式
    int len = vsnprintf((m_writebuffer + m_write_index), (WRITE_BUFFER_SIZE - m_write_index - 1), format, arg_list);

    // 写入数据超过缓冲区数据
    if(len >= (WRITE_BUFFER_SIZE - m_write_index - 1)) {
        va_end(arg_list);                                // 清空可变参列表
        return false;
    }
    m_write_index += len;
    va_end(arg_list);
    return true;
}

// 生成响应首行
bool HttpConn::add_response_statline(int status, const char* title)
{
    return add_response_info("%s %d %s\r\n", "HTTP/1.1", status, title);
}

// 生成响应头部
void HttpConn::add_response_headers(int content_length, const char* content_type)
{
    add_response_contentlen(content_length);
    add_response_contenttype(content_type);
    add_response_connstatus();
    add_response_blankline();
}

// 生成响应体
bool HttpConn::add_response_content(const char* content)
{
    return add_response_info("%s\n", content);
}

// 响应连接状态
bool HttpConn::add_response_connstatus()
{
    return add_response_info("Connection: %s\r\n", m_conn_status ? "keep-alive" : "close");
}

// 生成响应体长度
bool HttpConn::add_response_contentlen(int content_length)
{
    return add_response_info("Content-Length: %d\r\n", content_length);
}

// 生成响应体类型
void HttpConn::add_response_contenttype(const char* content_type)
{
    // 需要根据用户的请求，来指定响应体的类型
    add_response_info("Content-Type: %s\r\n", content_type);
}

// 生成响应头类型
bool HttpConn::add_response_blankline()
{
    return add_response_info("%s", "\r\n");
}

// 解析具体的POST请求体中的内容并保存
void HttpConn::parse_login_postinfo() 
{
    // 提取用户名和密码 username=zhang&password=9090
    int i = 0;
    for(i = 9; str_postinfo[i] != '&'; i++) {
        m_usrname[i-9] = str_postinfo[i];  // 记录zhang这段内容
    }
    m_usrname[i-9] = '\0';
    int j = 0;
    for(i = i + 10; str_postinfo[i] != '\0'; i++, j++) {
        m_usrpwd[j] = str_postinfo[i];     // 记录9090这段内容
    } 
    m_usrpwd[j] = '\0';
    Info("Parse got login/register info:\nm_usrname:%s , m_usrpwd:%s", m_usrname, m_usrpwd);
}

bool HttpConn::process_register_request()
{
    parse_login_postinfo();
    if(userdb_info.find(m_usrname) == userdb_info.end()) { // 是新用户可以注册
        char sql[1024] = {0};
        sprintf(sql, "insert into user values('%s', '%s')", m_usrname, m_usrpwd);
        bool flag = conn->updateDB(sql);
        init_userdb_info();                                // 刷新一下本地用户信息
        return flag;
    }
    else {
        return false;
    }
}

bool HttpConn::process_login_request()
{
    parse_login_postinfo();
    if((userdb_info.find(m_usrname) != userdb_info.end()) && 
       (userdb_info[m_usrname] == m_usrpwd)) {               // 信息校验成功
        return true;
    }
    else {
        return false;
    }
}

void HttpConn::parse_usrmsg_postinfo()
{
    char* finish_reason_str = strstr(str_postinfo, "message");              // 检查缓冲区中是否包含完整的 Json 数据包
    if(finish_reason_str == nullptr) {                                      // 没有收到消息
        return;
    }                                          
    Json::Value root;                                                       // 说明收到了一个完整的 JSON 数据包, 开始解析 JSON 数据
    Json::CharReaderBuilder reader;
    std::istringstream json_stream(str_postinfo);
    std::string errs;
    bool no_errors = Json::parseFromStream(reader, json_stream, &root, &errs);
    if(no_errors) {
        Json::Value message = root["message"];
        if (!message.isNull()) {       
            content_msg_str = message.asString();
            // printf("[User Question]: %s\n\n", content_msg_str.c_str());
            Info("[User Question]:\n%s", content_msg_str.c_str());
        }
        else {
            // printf("Content is missing or null!\n");
            Error("Content is missing or null!");
        }
    }
    else {                                                                  // JSON 解析失败，处理错误
        std::cerr << "JSON Parsing error:\n" << errs << std::endl;
        Error("User JSON data parsing failed!");
    }
}

std::string HttpConn::generateAIReply(std::string message)
{
    send_request_chat(message.c_str());
    return chat_reply_str;
}

void HttpConn::process_chatQA_request()
{
    parse_usrmsg_postinfo();

    // 创建一个用于响应用户的 JSON 对象
    Json::Value response;
    response["Server"] = "server_reply";

    // 处理客户端发送的 JSON 数据
    // 实现业务逻辑，根据用户的问题生成回复（ChatGPT交互核心）
    std::string response_content_str = generateAIReply(content_msg_str);
    response["message"] = response_content_str;

    // 将JSON对象转换为字符串，作为响应体消息
    Json::StreamWriterBuilder writer;
    reply_content_json = Json::writeString(writer, response);
    // std::cout << "Server Reply Json String: \n" << reply_content_json << std::endl;
    Info("Server Reply Json String:\n%s", reply_content_json.c_str());
}

void HttpConn::send_request_chat(const char *data)
{
    CURL *curl;
    CURLcode res;

    curl = curl_easy_init();                                                                            // 初始化Curl
    if(curl) {  
        // curl_easy_setopt(curl, CURLOPT_PROXY, "http://192.168.184.10:7890");                         // 设置代理服务器的地址
        struct curl_slist *headers = nullptr;                                                           // 设置请求头
        char header_auth[1000];
        sprintf(header_auth, "Authorization: Bearer %s", openai_key);
        headers = curl_slist_append(headers, "Content-Type: application/json");
        headers = curl_slist_append(headers, header_auth);
        
        char messagesTemplate[] = "[{\"role\": \"user\", \"content\": \"%s\"}]";                        // 构建请求数据
        char chatRequestTemplate[] = "{\"model\": \"%s\", \"messages\": %s}";
        char message[4096], payload_send_char_message[10240]; // 请求正文的消息
        sprintf(message, messagesTemplate, data);
        sprintf(payload_send_char_message, chatRequestTemplate, "gpt-3.5-turbo", message);  

        curl_easy_setopt(curl, CURLOPT_URL, "https://api.openai.com/v1/chat/completions");              // 设置Curl选项
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload_send_char_message);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, HttpConn::write_callback_chat);

        // curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, HttpConn::debug_callback);                     // 启用调试输出
        // curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);                                                 // 启用详细输出,-v选项
        
        res = curl_easy_perform(curl);                                                                  // 执行请求
        if(res != CURLE_OK) {
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
            // printf("请重新尝试提问哈~~\n");
            Error("curl_easy_perform() failed: %s\n请重新尝试提问哈~~", curl_easy_strerror(res));
        }
                
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);                                                                        // 清理Curl资源                           
    }
}

size_t HttpConn::write_callback_chat(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    size_t response_size = size * nmemb;                                        // 接收到的数据的总大小
    char *response = static_cast<char*>(malloc(response_size + 1));
    strncpy(response, ptr, response_size);
    response[response_size] = '\0';
    memcpy(chat_reply_buffer + buffer_offset, response, response_size);         // 将接收到的数据追加到缓冲区
    buffer_offset += response_size;
    free(response);

    char* finish_reason_str = strstr(chat_reply_buffer, "finish_reason");       // 检查缓冲区中是否包含完整的 Json 数据包
    char* error_msg_str = strstr(chat_reply_buffer, "error");
    if(finish_reason_str != nullptr) {                                          // 说明收到了一个完整的 JSON 数据包
        Json::Value root;                                                       // 解析JSON数据
        Json::CharReaderBuilder reader;
        std::istringstream json_stream(chat_reply_buffer);
        std::string errs;
        bool no_errors = Json::parseFromStream(reader, json_stream, &root, &errs);
        if(no_errors) {
//            Json::StreamWriterBuilder writer;                                 // 显示解析后的字符串
//            std::string res = Json::writeString(writer, root);
//            std::cout << "Parsed Json String: \n" << res << std::endl;

            if (root.isMember("choices") && root["choices"].isArray()) {        // "choices" 字段存在且是一个数组
                Json::Value choices = root["choices"];
                if (!choices.empty() && choices[0].isObject()) {                // choices 不为空且第一个元素是对象类型
                    Json::Value message = choices[0]["message"];
                    if (!message.empty() && message.isObject()) {               // message 不为空且是对象类型
                        Json::Value content = message["content"];
                        if (!content.isNull()) {                                // content 存在
                            Json::Value finish_reason = choices[0]["finish_reason"];
                            if (!finish_reason.isNull()) {                      // finish_reason 存在
                                chat_reply_str = content.asString();
                                Info("[GPT3 Answer]:\n%s", chat_reply_str.c_str());
                            }
                            else {
                                Error("Finish Reason is missing!");
                            }
                        }
                        else {
                            Error("Content is missing or null!");
                        }
                    }
                }
                // 清空缓冲区和偏移量
                memset(chat_reply_buffer, 0, sizeof (chat_reply_buffer));
                buffer_offset = 0;
            }
            else {
                Error("Response json data is error!");
            }
        }
        else {
            // JSON 解析失败，处理错误
            std::cerr << "JSON Parsing error:\n" << errs << std::endl;
            memset(chat_reply_buffer, 0, sizeof(chat_reply_buffer));
            buffer_offset = 0;

        }
    }
    else if(error_msg_str != nullptr) {                                 // 说明收到了错误消息
        Json::Value root;                                               // 解析JSON数据
        Json::CharReaderBuilder reader;
        std::istringstream json_stream(chat_reply_buffer);
        std::string errs;
        bool no_errors = Json::parseFromStream(reader, json_stream, &root, &errs);
        if(no_errors) {
            Json::Value error_msg = root["error"]["message"];
            std::string error_str = error_msg.asString();
            printf("\n[Server Error]: %s\n", error_str.c_str());
            memset(chat_reply_buffer, 0, sizeof(chat_reply_buffer));
            buffer_offset = 0;
            return -1;
        }
        else {
            // JSON 解析失败，处理错误
            std::cerr << "JSON Parsing error: " << errs << std::endl;
            Error("User JSON data parsing failed!");
        }
        memset(chat_reply_buffer, 0, sizeof(chat_reply_buffer));
        buffer_offset = 0;
    }

    usleep(1000*400); // 间隔 400ms 后再次获取数据
    return response_size;
}

int HttpConn::debug_callback(CURL *curl, curl_infotype type, char *data, size_t size, void *userptr) {
    // 输出请求数据
    if (type == CURLINFO_HEADER_OUT || type == CURLINFO_DATA_OUT) {
        // printf("Sent Data:\n%.*s\n", (int)size, data);
        Info("Sent Data:\n%.*s", (int)size, data);
    }
    return 0;
}
