#include "dbconn_pool.h"
#include <jsoncpp/json/json.h>
#include <fstream>
#include <thread>
#include <iostream>

// 单例，得到当前的唯一实例
DBConnPool* DBConnPool::getDBConnPool()
{
    static DBConnPool pool;
    return &pool;
}

DBConnPool::DBConnPool()
{
    if(!parseJsonFile()) {  // 加载配置文件
        std::cout << "the mysql config info load failed !!!" << std::endl; // TODO Error_Log
    }
    for(int i=0; i<m_minConnSize; i++) {
        addDBConn();
    }
    // 创建两个线程
    std::thread th_produceConn(&DBConnPool::produceConn, this);    // 生产连接池中的数据库连接，当前唯一实例对象this是指这个任务对象的所有者
    std::thread th_destoryConn(&DBConnPool::destoryConn, this);    // 检测非活跃的连接，看是否可以进行销毁, TODO 可用定时器来优化
    th_produceConn.detach(); // 设置线程分离
    th_destoryConn.detach();
}

DBConnPool::~DBConnPool()
{
    while(!m_dbConnQueue.empty()) {
        // 将队列中的所有连接依次释放
        MySQLConn* conn = m_dbConnQueue.front(); // 共享资源，析构时不用加锁
        m_dbConnQueue.pop();
        delete conn;
    }
}

void DBConnPool::addDBConn()
{
    MySQLConn* conn = new MySQLConn();
    conn->connectToDB(m_host, m_port, m_username, m_passwrod, m_db_name);
    conn->refreshActiveTime(); // 更新最初建立连接的时间戳
    m_dbConnQueue.push(conn);
}

bool DBConnPool::parseJsonFile()
{
    std::ifstream inputFile("../config/db_config.json");
    if(!inputFile.is_open()) {
        std::cerr << "Failed to open mysql config file." << std::endl;
        return false;
    }
    Json::Reader rd;
    Json::Value root;
    rd.parse(inputFile, root);
    if(root.isObject()) {
        m_host = root["host"].asString();
        m_port = root["port"].asInt();
        m_username = root["username"].asString();
        m_passwrod = root["password"].asString();
        m_db_name = root["db_name"].asString();

        m_maxConnSize = root["max_ConnSize"].asInt();
        m_minConnSize = root["min_ConnSize"].asInt();
        m_thTimeout = root["thTimeout"].asInt();
        m_maxFreeTime = root["maxFreeTime"].asInt();
        return true;
    }
    return false;
}


// PS: 这里的条件变量 m_cv（阻塞了生产者和消费者） 但 notify_all()唤醒了生产者线程，又唤醒了消费者线程
// 同时唤醒会有问题吗？ 不会, while 中的会一直判断连接数量进行阻塞
[[noreturn]] void DBConnPool::produceConn()
{
    while(true) {
        std::unique_lock<std::mutex> lock(m_mtx);
        // 连接够用，需要阻塞生产者, 使用while是保证连接数不够用才退出进行添加操作
        while(m_dbConnQueue.size() >= m_minConnSize) {
            m_cv.wait(lock);
        }
        if(m_dbConnQueue.size() < m_maxConnSize) {
            addDBConn();
            m_cv.notify_all(); // 添加了一个新连接，来唤醒消费者线程
        }
    }
}

[[noreturn]] void DBConnPool::destoryConn()
{
    while(true) {
        // 睡眠，让其每隔一段时间后去检测
        std::this_thread::sleep_for(std::chrono::milliseconds(5000));
        std::lock_guard<std::mutex> lock(m_mtx);
        while(m_dbConnQueue.size() >= m_minConnSize) { // 检测非活跃连接同时维持一个最小连接数
            MySQLConn* conn = m_dbConnQueue.front();
            if(conn->getConnAliveTime() >= m_maxFreeTime) { // 判断是否超过了空闲时长
                m_dbConnQueue.pop();
                delete conn;
            }
            else {
                break;
            }
        }
    }
}

std::shared_ptr<MySQLConn> DBConnPool::getDBConn()
{
    std::unique_lock<std::mutex> lock(m_mtx);
    while(m_dbConnQueue.empty()) { // 若连接池已空，需要让当前线程阻塞
        // 等待一段时间后，数据库连接队列还是为空，会继续阻塞
       if(std::cv_status::timeout == m_cv.wait_for(lock, std::chrono::milliseconds(m_thTimeout))) {
           if(m_dbConnQueue.empty()) {
               continue; // 保证队列为空时，一直阻塞住
           }
       }
    }

    // 当用户获取到一个数据库连接对象，并完成了相应的数据库操作
    // 后续不在需要使用时，需要放回连接池进行回收
    // 可以使用共享智能指针std::shared_ptr，但是不是让自动释放内存，而是自己指定删除器来进行指针的地址回收

    // 不空时，拿到一个可用的数据库连接
    std::shared_ptr<MySQLConn> conn_ptr(m_dbConnQueue.front(), [this](MySQLConn* conn) { // this，可使用当前类成员及函数
        std::lock_guard<std::mutex> lock(m_mtx);
        conn->refreshActiveTime();  // 更新为回收的时间点
        m_dbConnQueue.push(conn);   // 重新将连接放回连接池，队列是共享资源需要加锁
    });
    m_dbConnQueue.pop();
    m_cv.notify_all(); // 唤醒生产者线程
    return conn_ptr;
}
