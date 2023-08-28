#include "epoller.h"
#include <sys/epoll.h>
#include <unistd.h> // close()
#include <fcntl.h>

void Epoller::add_fd(int epollfd, int fd, bool one_shot)
{
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLRDHUP;
    if(one_shot) {
        event.events | EPOLLONESHOT;
    }
    // TODO, 错误检查
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    set_unblocking(fd);
}

void Epoller::remove_fd(int epollfd, int fd)
{
    // TODO, 错误检查
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

// 重置socket上的EPOLLONESHOT事件，以确保下一次可读时，EPOLLIN事件能被触发
void Epoller::modify_fd(int epollfd, int fd, int ev)
{
    epoll_event event;
    event.events = ev | EPOLLONESHOT | EPOLLRDHUP; // 添加注册事件
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event); // 重置event事件
}

void Epoller::set_unblocking(int fd) // 设置fd为非阻塞
{
    // 获取fd状态
    int oldflag = fcntl(fd, F_GETFL);

    // 修改状态
    int ret = fcntl(fd, F_SETFL, oldflag | O_NONBLOCK);
    
}
