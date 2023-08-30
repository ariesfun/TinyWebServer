#include "http_conn.h"
#include <cstdio>
#include <cstring>
#include <sys/epoll.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h> // 文件操作
#include <unistd.h> // close()
#include <sys/mman.h> // mmap()
#include <sys/uio.h> // writev()

#include "logger.h"
using namespace ariesfun::log;

int HttpConn::m_http_epollfd = -1;
int HttpConn::m_client_cnt = 0;

// 网站资源根目录
const char* web_root_src = "/home/ariesfun/Resume_Projects/TinyWebServer/resources"; 

void HttpConn::init(int sockfd, const sockaddr_in &addr)
{
    m_http_sockfd = sockfd; // 保存当前连接的客户端信息
    m_http_addr = addr;
    int reuse = 1; // 设置端口复用
    setsockopt(m_http_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    // proactor模式，先让HttpConn类来处理
    epoller->add_fd(m_http_epollfd, sockfd, true);
    m_client_cnt++;

    my_init();
}

void HttpConn::my_init() // 初始化解析或响应要保存的信息
{
    m_cur_mainstate = CHECK_STATE_REQUESTLINE; // 初始化为解析的请求首行
    m_cur_index = 0;
    m_start_line = 0;
    m_read_index = 0;

    // 保存的HTTP请求信息，初始化
    m_url = nullptr;
    m_method = GET;
    m_version = nullptr;
    m_hostaddr = nullptr;
    m_conn_status = false;
    m_content_length = 0;

    m_write_index = 0;
}

void HttpConn::close_conn() 
{
    if(m_http_sockfd != -1) {
        epoller->remove_fd(m_http_epollfd, m_http_sockfd);
        m_http_sockfd = -1;
        m_client_cnt--;
    }
}

bool HttpConn::read() // 将数据读入到读缓冲区，解析
{
    // 循环读直到客户端断开
    if(m_read_index >= READ_BUFFER_SIZE) {
        return false;
    }
    // 读到的字节
    int read_bytes = 0;
    while(true) {
        // 从(m_read_buf + m_read_idx)索引出开始保存数据，大小是(READ_BUFFER_SIZE - m_read_idx)
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
    Info("read data is:\n%s", m_readbuffer);
    return true;
}

bool HttpConn::write() // 响应存入到写缓冲区，发送
{
    int temp = 0;
    int send_bytes = 0; // 已发送的字节数
    int to_send_bytes = m_write_index; // 待发送的字节

    if(to_send_bytes == 0) { // 没有要发送的数据了，结束响应
        epoller->modify_fd(m_http_epollfd, m_http_sockfd, EPOLLIN);
        my_init();
        return true;
    }
    while(true) { // 进行分散写
        // iovec用于执行读取和写入操作
        // writev函数将多块数据一并写入套接字，避免了多次数据复制和多次系统调用的开销
        temp = writev(m_http_sockfd, m_iv, m_iv_cnt);
        if(temp <= -1) {
            // 若TCP缓冲没有空间，则等待下一轮EPOLLOUT事件
            // 此时虽无法立即收到该客户端的下一个请求，但可以保证连接的稳定性
            if(errno == EAGAIN) { // 修改epoll监听事件来等待下一次写事件
                epoller->modify_fd(m_http_epollfd, m_http_sockfd, EPOLLOUT);
                return true;
            }
            free_mmap();
            return false;
        }
        to_send_bytes -= temp; // 更新信息，还剩下多少字节需要发送
        send_bytes += temp; // 已经发送了多少字节
        if(to_send_bytes <= send_bytes) {
            // 发送HTTP响应成功, 根据HTTP请求中的Connection连接状态
            // 来决定是否关闭连接
            free_mmap();
            if(m_conn_status) {
                my_init();
                epoller->modify_fd(m_http_epollfd, m_http_sockfd, EPOLLIN);
                return true;
            }
            else {
                epoller->modify_fd(m_http_epollfd, m_http_sockfd, EPOLLIN);
                return false;
            }
        }
    }
}

// 由线程池中的工作线程调用，这是处理HTTP请求的入口函数
void HttpConn::start_process() 
{
    // 1.解析HTTP请求，读取数据(进行业务的逻辑处理)
    HTTP_CODE read_ret = parse_request(); // 根据不同状态来处理业务，状态会发生转移
    if(read_ret == NO_REQUEST) { // 请问不完整
        epoller->modify_fd(m_http_epollfd, m_http_sockfd, EPOLLIN);
        return;
    }

    // 2.生成响应，写数据发给客户端
    bool write_ret = process_response(read_ret); // 根据请求信息，处理对应的响应信息
    if(!write_ret) {
        close_conn();
    }

    // 3.返回数据给客户端
    epoller->modify_fd(m_http_epollfd, m_http_sockfd, EPOLLOUT);
}

// 主状态机(有限状态机)，解析请求主要是读数据操作
HttpConn::HTTP_CODE HttpConn::parse_request()
{
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE request_ret = NO_REQUEST; 
    char* line_data = nullptr;
    while((m_cur_mainstate == CHECK_STATE_CONTENT) && (line_status = LINE_OK) || // 主状态机解析请求体，从状态机解析行成功
          ((line_status = parse_detail_line()) == LINE_OK)) { // 每次解析一行数据
        // 解析到了请求体和一行数据
        line_data = get_linedata(); // 获得一行数据
        m_start_line = m_cur_index; // 更新解析的行首位置
        printf("got 1 http line data:\n%s", line_data);

        switch (m_cur_mainstate) // 主状态机 (有效状态机)
        {
            case CHECK_STATE_REQUESTLINE:
            {
                request_ret = parse_request_line(line_data);
                if(request_ret == BAD_REQUEST) { // 语法错误
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
                else if(request_ret == GET_REQUEST) { // 获得了一个完整的客户请求
                    return do_request();
                }
                break;
            }
            case CHECK_STATE_CONTENT:
            {
                request_ret = parse_request_content(line_data);
                if(request_ret == GET_REQUEST) {
                    return do_request();
                }
                line_status = LINE_OPEN; // 行不完整
                break;
            }          
            default:
                return INTERNAL_ERROR;
                break;
        }
    }
    return NO_REQUEST;
}

// 读信息，解析请求行
// 'GET /index.html HTTP/1.1' : 请求方法 | 目标URL | HTTP版本
HttpConn::HTTP_CODE HttpConn::parse_request_line(char* text)
{
    m_url = strpbrk(text, " \t"); // 判断字符空格或制表符哪个先在text中出现，并返回位置
    *(m_url++) = '\0';
    // GET\0/index.html HTTP/1.1

    char* method = text;
    if(strcasecmp(method, " GET") == 0) {
        m_method = GET; // 得到请求方法
    }
    else {
        return BAD_REQUEST;
    }
    // m_url: /index.html HTTP/1.1
    m_version = strpbrk(m_url, " \t");
    if(m_version == nullptr) {
        return BAD_REQUEST;
    }
    *(m_version++) = '\0';
    // m_url: /index.html\0HTTP/1.1
    if(strcasecmp(m_version, "HTTP/1.1") != 0) {
        return BAD_REQUEST;
    }

    // http://192.168.1.1:8888/index.html
    if(strncasecmp(m_url, "http://", 7) == 0) {
        m_url += 7; // 192.168.1.1:8888/index.html
        m_url = strchr(m_url, '/'); // /index.html
    }
    if(m_url == nullptr || m_url[0] != '/') {
        return BAD_REQUEST;
    }
    
    // 更新检查状态
    // 主状态机状态变为检查请求头
    m_cur_mainstate = CHECK_STATE_HEADER;

    return NO_REQUEST;
}

// 解析请求头
HttpConn::HTTP_CODE HttpConn::parse_request_headers(char* text)
{
    if(text[0] == '\0') { // 为空时，头部解析完成
        if(m_content_length != 0) { // 如果消息体有内容，状态机状态需要转移
            m_cur_mainstate = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST; // 得到了一个完整的请求头部信息
    } // 下面只做常见的请求头的解析
    else if(strncmp(text, "Host:", 5) == 0) { // 主机域名
        text += 5;
        // 192.168.184.10:8888, 此时指针指向'192'的'1'位置
        text += strspn(text, " \t"); // 移动指针到第一个不在这个字符集合中的字符处
        m_hostaddr = text;
        Info("host_addr:%s", m_hostaddr);
    }
    else if(strncmp(text, "Connection:", 11) == 0) { // 获取连接状态
        text += 11;
        text += strspn(text, " \t");
        if(strcmp(text, "keep-alive") == 0) {
            m_conn_status = true;
        }
        Info("http connection status: %d", m_conn_status);
    }
    else if(strncmp(text, "Content-Length:", 15) == 0) { // 内容长度
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text);
        Info("content_length: %d", m_content_length);
    }
    else { // 其他，出现未知头部字段
        Info("unknow header %s\n", text);
    }
    return NO_REQUEST;
}

// 解析请求体内容
HttpConn::HTTP_CODE HttpConn::parse_request_content(char* text)
{
    if(m_read_index > (m_content_length + m_cur_index)) { // 指针能移动到请求体部分，说明可以拿到这部分信息

        // 解析具体的逻辑未实现TODO

        text[m_content_length] = '\0';
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

// 从状态机处理
HttpConn::LINE_STATUS HttpConn::parse_detail_line() // 解析一行数据报文，判断\r\n
{
    char ch;
    for(; m_cur_index < m_read_index; m_cur_index++) {
        ch = m_readbuffer[m_cur_index];
        if(ch == '\r') {
            if((m_cur_index + 1) == m_read_index) { // 是下一次要读的位置
                return LINE_OPEN; // 行不完整
            }
            else if(m_readbuffer[m_cur_index + 1] == '\n') {
                m_readbuffer[m_cur_index++] = '\0'; // '\r'置空
                m_readbuffer[m_cur_index++] = '\0'; // '\n'置空
                return LINE_OK;
            }
            return LINE_BAD; // 行出错
        }
        else if(ch == '\n') { // 已经到行尾了
            if(m_cur_index > 1 && (m_readbuffer[m_cur_index-1] == '\r')) {
                m_readbuffer[m_cur_index-1] = '\0';
                m_readbuffer[m_cur_index++] = '\0'; // 当前位置置空后后移
                return LINE_OK;
            }
            return LINE_BAD;
        }
        return LINE_OPEN; // 数据不完整
    }
}   

// 当得到一个完整正确的HTTP请求时，我们就分析目标文件的属性
// 如果目标文件（非目录）存在、且是对所有用户可读的状态
// 则使用mmap将文件映射到内存地址m_file_adress处，并告诉调用者获取文件成功
HttpConn::HTTP_CODE HttpConn::do_request()
{
    strcpy(m_real_file, web_root_src);
    int len = strlen(web_root_src);
    if(len < FILEPATH_LEN-1) {
        strncat(m_real_file, m_url, FILEPATH_LEN - len - 1); // 追加信息，拼接成要访问本地的文件路径
    }
    Info("the client request file path: %s", m_real_file);
    // 先获取m_real_file文件相关的状态信息，-1失败，0成功
    if(stat(m_real_file, &m_file_status) < 0) {
        return NO_REQUEST;
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

// 生成响应，发送给客户端
bool HttpConn::process_response(HTTP_CODE ret)
{

}