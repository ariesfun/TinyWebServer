#ifndef TINYWEBSERVER_THREAD_POOL_H
#define TINYWEBSERVER_THREAD_POOL_H

#include <pthread.h>
#include <list>
#include <cstdio>
#include <cstring>

#include "locker.h" // 确保线程安全和同步

#include "logger.h"
using namespace ariesfun::log;

// 模板类写法，便于代码复用，模板参数T是任务类
template<typename T>
class ThreadPool {
public:
    ThreadPool(int thread_num = 8, int max_requests = 10000); // 设置默认值
    ~ThreadPool();

    bool add_task(T* request); // 添加任务

private:
    // 静态的线程执行函数,循环取出队列中的任务并执行
    static void* worker(void* arg);

    void run(); // 真正执行前，先进行阻塞判断

private:
    int m_thread_num; // 线程数量
    pthread_t* m_threads; // 线程池数组，指向pthread_t*类型
    int m_max_requests; // 线程池可接受的最大请求数

    std::list<T*> m_workqueue; // 请求队列
    bool is_stop_thread; // 线程结束的状态
    Locker m_queuelocker; // 互斥锁
    Sem m_queuestatus; // 判断是否有任务要处理
};

// 模板类的实现一般都写在一个文件里（类模板的成员函数运行阶段才去创建，不会创建实现）
template<typename T>
ThreadPool<T>::ThreadPool(int thread_num, int max_requests) :
    m_thread_num(thread_num), m_max_requests(max_requests), is_stop_thread(false), m_threads(nullptr)
{
    if(thread_num <= 0 || max_requests <= 0) {
        throw std::exception();
    }

    m_threads = new pthread_t[m_thread_num]; // 动态创建线程数组，记得delete
    if(m_threads == nullptr) {
        throw std::exception();
    }
    // 创建指定的线程数，并将它们设置为脱离线程，以便让他们自己释放资源
    for(int i = 0; i < thread_num; i++) {
        Info("create the %dth thread", i);
        printf("create the %dth thread", i); // TODO-DEL

        // c++的线程执行函数worker，必须是静态函数
        // 第三个参数是一个指向函数的指针，该函数将在新线程中执行
        // 在这里，worker 是一个函数指针，表示新线程将执行名为 worker 的函数
        // 第四个参数是传递给 worker 函数的参数
        // 在这里this是一个指向当前对象的指针，以便在新线程中执行的worker函数能使用当前对象的成员数据和方法
        if(pthread_create(&m_threads[i], nullptr, worker, this) != 0) {
            delete[] m_threads;
            throw std::exception();
        }

        // 设置线程分离，让其在后台运行
        if(pthread_detach(m_threads[i])) { // 返回0执行成功
            delete[] m_threads;
            throw std::exception();
        }
    }
}

template<typename T>
ThreadPool<T>::~ThreadPool()
{
    is_stop_thread = ture;
    // 线程数组中是分离线程，会自动释放资源
}

template<typename T>
bool ThreadPool<T>::add_task(T* request)
{
    m_queuelocker.lock(); // 操作共享数据
    if(m_workqueue.size() > m_max_requests) {
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    // 有任务需要处理,信号量增加
    // 用于后续判读线程是阻塞，还是继续执行
    m_queuestatus.post();
    return ture;
}

// 静态的线程执行函数
template<typename T>
void* ThreadPool<T>::worker(void* arg) // 这里接受的参数是this当前对象
{
    ThreadPool* pool = (ThreadPool*)arg;
    pool->run();
    return pool;
}

// 进行阻塞判断
template<typename T>
void ThreadPool<T>::run()
{
    while(!is_stop_thread) { // 线程运行中
        m_queuestatus.wait(); // 信号量有值就不会阻塞(减1)
        m_queuelocker.lock();
        if(m_workqueue.empty()) {
            m_queuelocker.unlock();
            continue;
        }
        T* request = m_workqueue.front(); // 从队头取出
        m_workqueue.pop_front();
        m_queuelocker.unlock();
        if(request) {
            request->startprocess(); // 交给Http处理类，真正开始处理任务
        }
    }
}

#endif //TINYWEBSERVER_THREAD_POOL_H
