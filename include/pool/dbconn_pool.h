#pragma
#include "mysql_conn.h"
#include <queue>
#include <mutex>
#include <condition_variable>

// 数据库连接池类，需要设置为单例模式
class DBConnPool {
public:
    static DBConnPool* getDBConnPool();
    DBConnPool(const DBConnPool &) = delete;
    DBConnPool& operator = (const DBConnPool &) = delete;
    std::shared_ptr<MySQLConn> getDBConn(); // 让用户可以获取到一个可用的数据库连接，相当于消费者线程

private:
    DBConnPool();
    ~DBConnPool();
    bool parseJsonFile();
    void addDBConn();

    [[noreturn]] void produceConn(); // 线程池的任务函数
    [[noreturn]] void destoryConn();

private:
    std::string m_host;
    unsigned short m_port;
    std::string m_username;
    std::string m_passwrod;
    std::string m_db_name;

    int m_maxConnSize;                      // 连接池维持的最大连接数
    int m_minConnSize;                      // 最小连接数
    int m_thTimeout;                        // 指定线程超时的时长(ms)，即用户阻塞等待之后可以再次获取一个数据库连接
    int m_maxFreeTime;                      // 设置最大的空闲时长(ms)，超过就移除非活跃的数据库连接

private:
    std::queue<MySQLConn*> m_dbConnQueue;   // 存储数据库连接对象的队列，线程的共享资源
    std::mutex m_mtx;                       // 互斥锁，避免共享数据竞争的问题
    std::condition_variable m_cv;           // 条件变量，用于阻塞生产者线程
};
