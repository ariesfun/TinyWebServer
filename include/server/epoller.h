#ifndef TINYWEBSERVER_EPOllER_H
#define TINYWEBSERVER_EPOllER_H

class Epoller {
public:
    Epoller() {}
    ~Epoller() {}

    void add_fd(int epollfd, int fd, bool one_shot);
    void remove_fd(int epollfd, int fd);
    void modify_fd(int epollfd, int fd, int ev);
    void set_unblocking(int fd);

};

#endif // TINYWEBSERVER_EPOllER_H
