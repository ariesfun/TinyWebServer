#include "epoller.h"
#include <sys/epoll.h>
#include <unistd.h> // close()
#include <fcntl.h>
#include <cstring>

#include "logger.h"
using namespace ariesfun::log;

void Epoller::add_fd(int epollfd, int fd, bool one_shot)
{
    epoll_event event;
    // memset(&event, 0, sizeof(event)); // 初始化event结构体
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLRDHUP; //  默认用水平触发, EPOLLET是改为边沿触发
    if(one_shot) {
        event.events |= EPOLLONESHOT;
    }
    int ret = epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    if(ret == -1) {
        Error("add event failed!")
    }
    set_unblocking(fd);
}

void Epoller::remove_fd(int epollfd, int fd)
{
    int ret = epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    if(ret == -1) {
        Error("remove event failed!");
    }
    close(fd);
}

// 重置socket上的EPOLLONESHOT事件，以确保下一次可读时，EPOLLIN事件能被触发
void Epoller::modify_fd(int epollfd, int fd, int ev)
{
    epoll_event event;
    // memset(&event, 0, sizeof(event)); // 初始化event结构体
    event.data.fd = fd;
    event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP; // 添加注册事件
    int ret = epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event); // 重置event事件
    if(ret == -1) {
        Error("modify event failed!");
    }
}

void Epoller::set_unblocking(int fd) // 设置fd为非阻塞状态
{
    // 获取fd状态
    int oldflag = fcntl(fd, F_GETFL);
    if(oldflag == -1) {
        Error("get fd status failed!");
    }
    // 修改状态
    int ret = fcntl(fd, F_SETFL, oldflag | O_NONBLOCK);
    if(ret == -1) {
        Error("edit fd status failed!");
    }
}
                                        