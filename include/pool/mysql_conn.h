#pragma once

#include <string>
#include <chrono>
#include <mysql/mysql.h>

// 封装MySQL操作的API
class MySQLConn {
public:
    MySQLConn(); // 初始化及释放数据库连接
    ~MySQLConn();

    bool connectToDB(std::string host, unsigned short port,
                     std::string username, std::string password, std::string db_name); // 连接数据库
    bool updateDB(std::string sql);         // 更新数据库 (insert、update、delete)
    bool selectDB(std::string sql);         // 查询数据库
    bool processQueryResults();             // 遍历查询得到的结果集
    std::string getFieldValue(int index);   // 得到结果集中的字段值
    bool executeTransaction();              // 事务操作，设置事务提交
    bool commitTransaction();               // 提交事务
    bool rollbackTransaction();             // 事务回滚

public:
    void refreshActiveTime();         // 刷新用户使用数据库连接的时间
    long long getConnAliveTime();     // 得到当前连接存活的时长

private:
    void freeQueryRes();              // 释放查询的结果集

private:
    MYSQL* m_conn = nullptr;          // 连接析构时会释放
    MYSQL_RES* m_query_res = nullptr; // 保存查询的结果集，地址没有释放（数据读完了就需要释放）
    MYSQL_ROW m_row_record = nullptr; // 保存查询的每行记录，其地址指向查询的结果集

    std::chrono::steady_clock::time_point m_time; // 获得当前的绝对时钟
};
