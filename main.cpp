#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <libgen.h> // basename
#include <cerrno>
#include <csignal>
#include <cassert>

#include "locker.h"
#include "thread_pool.h"
#include "http_conn.h"

#define MAX_CLIENT_FD 65535 // 可连接的最大客户端数量
#define MAX_LISTEN_EVENT_NUM 10000 // 一次监听时最大的事件数

#include "logger.h"
using namespace ariesfun::log;


void handle_sig(int sig, void(handler)(int)) // 进行信号捕捉
{
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    sigfillset(&sa.sa_mask); // 设置临时的阻塞信号集
    assert(sigaction(sig, &sa, nullptr) != -1); // 进行信号注册
}

int main(int argc, char* argv[])
{
    // 日志配置
    Logger::getInstance()->open("../log_info/LOG_INFO.log");
    Logger::getInstance()->log_setlevel(Logger::INFO);
    Logger::getInstance()->log_maxsize(2000);


    // TODO, 可以放入server模块
    // Server的处理部分
    // 服务器模拟的是Proactor异步网络模式
    // 在主线程中去监听连接事件，有读事件时取出（给了HttpConn类处理），封装成任务对象，交给线程池中的子线程来处理
    if(argc <= 1) {
        Error("usage format: ./%s port_number", basename(argv[0]));
        printf("usage format: ./%s port_number\n", basename(argv[0]));
        exit(-1);
    }
    int ser_port = atoi(argv[1]);
    handle_sig(SIGPIPE, SIG_IGN); // 处理关闭连接时的SIGPIPE信号

    HttpConn* client_info = new HttpConn[MAX_CLIENT_FD]; // 保存客户端信息的数组




    return 0;
}
