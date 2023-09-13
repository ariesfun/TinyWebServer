#ifndef TINYWEBSERVER_LOCKER_H
#define TINYWEBSERVER_LOCKER_H

#include <pthread.h>
#include <exception>
#include <semaphore.h>

// 保证线程同步机制（互斥量类、条件变量类、信号量类）

// 互斥锁类
class Locker {
public:
    Locker() {
        if(pthread_mutex_init(&m_mutex, nullptr) != 0) {
            throw std::exception(); // 抛出异常
        }
    }
    ~Locker() {
        pthread_mutex_destroy(&m_mutex);
    }

    bool lock() {
        return pthread_mutex_lock(&m_mutex) == 0;
    }

    bool unlock() {
        return pthread_mutex_unlock(&m_mutex) == 0;
    }

    pthread_mutex_t* get_mutex() { // 获取成员的互斥锁
        return &m_mutex;
    }

private:
    pthread_mutex_t m_mutex;
};

// 条件变量类
class Condition {
public:
    Condition() {
        if(pthread_cond_init(&m_condition, nullptr) != 0) {
            throw std::exception();
        }
    }
    ~Condition() {
        pthread_cond_destroy(&m_condition);
    }

    // 当前线程阻塞，直到另一个线程发来通知时
    // 调用pthread_cond_signal或pthread_cond_broadcast 来唤醒等待在条件变量上的线程
    // 如果等待成功（即线程被唤醒），函数返回true，否则返回false
    bool wait(pthread_mutex_t* mutex) {
        return pthread_cond_wait(&m_condition, mutex) == 0;
    }

    // 与前面作用相似它允许在一定的时间范围内等待, 超时等待
    bool timedwait(pthread_mutex_t* mutex, struct timespec t) {
        return pthread_cond_timedwait(&m_condition, mutex, &t) == 0;
    }

    // 唤醒线程，让条件变量增加
    bool signal() { // 唤醒一个或多个
        return pthread_cond_signal(&m_condition) == 0;
    }
    bool broadcast() { // 唤醒所有
        return pthread_cond_broadcast(&m_condition) == 0;
    }

private:
    pthread_cond_t m_condition;
};

// 信号量类，多线程编程板块
class Sem {
public:
    Sem() {
        if(sem_init(&m_sem, 0, 0) != 0) {
            throw std::exception();
        }
    }
    Sem(int num) {
        if(sem_init(&m_sem, 0, num) != 0) {
            throw std::exception();
        }
    }
    ~Sem() {
        sem_destroy(&m_sem);
    }

    // 等待信号量
    bool wait() {
        return sem_wait(&m_sem) == 0;
    }
    // 增加信号量
    bool post() {
        return sem_post(&m_sem) == 0;
    }

private:
    sem_t m_sem;
};

#endif //TINYWEBSERVER_LOCKER_H
