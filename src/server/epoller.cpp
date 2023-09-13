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
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLRDHUP; //  默认用水平触发, EPOLLET是改为边沿触发
    if(one_shot) {
        event.events |= EPOLLONESHOT;
    }
    int ret = epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    if(ret == -1) {
        Error("\nadd event failed!\n")
    }
    set_unblocking(fd);
}

void Epoller::remove_fd(int epollfd, int fd)
{
    int ret = epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, nullptr);
    if(ret == -1) {
        int saved_errno = errno;
        Error("\nremove event failed: %s\n", strerror(saved_errno));
    }
    ret = close(fd);
    if(ret == -1) {
        int saved_errno = errno;
        Error("\nclose fd failed: %s\n", strerror(saved_errno));
    }
}

// 重置socket上的EPOLLONESHOT事件，以确保下一次可读时，EPOLLIN事件能被触发
void Epoller::modify_fd(int epollfd, int fd, int ev)
{
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP; // 添加注册事件
    int ret = epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event); // 重置event事件
    if(ret == -1) {
        Error("\nmodify event failed!\n");
    }
}

void Epoller::set_unblocking(int fd) // 设置fd为非阻塞状态
{
    // 获取fd状态
    int oldflag = fcntl(fd, F_GETFL);
    if(oldflag == -1) {
        Error("\nget fd status failed!\n");
    }
    // 修改状态
    int ret = fcntl(fd, F_SETFL, oldflag | O_NONBLOCK);
    if(ret == -1) {
        Error("\nedit fd status failed!\n");
    }
}
                                        