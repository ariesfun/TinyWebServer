#include "mysql_conn.h"

MySQLConn::MySQLConn()
{
    m_conn = mysql_init(nullptr);
    mysql_set_character_set(m_conn, "utf8"); // 设置字符编码
}

MySQLConn::~MySQLConn()
{
    if(m_conn != nullptr) {
        mysql_close(m_conn);
    }
    freeQueryRes(); // 查询的结果集不空时，也要释放掉
}

bool MySQLConn::connectToDB(std::string host, unsigned short port,
                            std::string username, std::string password, std::string db_name) {

    MYSQL* conn = mysql_real_connect(m_conn, host.c_str(), username.c_str(), password.c_str(),
                                     db_name.c_str(), port, nullptr, 0);
    if(conn == nullptr) {
        return false;
    }
    return true;
}

bool MySQLConn::updateDB(std::string sql)
{
    if(mysql_query(m_conn, sql.c_str())) { // 成功返回0，失败返回非0
        return false;
    }
    return true;
}

bool MySQLConn::selectDB(std::string sql)
{
    freeQueryRes(); // 先清空
    if(mysql_query(m_conn, sql.c_str())) {
        return false;
    }
    // 保存查询的结果集
    m_query_res = mysql_store_result(m_conn);
    return true;
}

bool MySQLConn::processQueryResults()
{
    if(m_query_res != nullptr) {
        m_row_record = mysql_fetch_row(m_query_res); // 会返回一个指针数组(每个元素都是char*类型)，是二级指针
        if(m_row_record != nullptr) {
            return true;
        }
    }
    return false;
}

std::string MySQLConn::getFieldValue(int index)
{
    int colNum = mysql_num_fields(m_query_res);
    if(index >= colNum || index < 0) {
        return " "; // 返回空串
    }
    char* value = m_row_record[index]; // 对于二进制数据中间是有'\0'的，直接转成字符串会有问题
    unsigned long len = mysql_fetch_lengths(m_query_res)[index];
    return std::string(value, len); // 将指定长度的字符转成string
}

bool MySQLConn::executeTransaction()
{
    return mysql_autocommit(m_conn, false); // 第二个参数即是否为自动提交，false为手动提交
}

bool MySQLConn::commitTransaction()
{
    return mysql_commit(m_conn);
}

bool MySQLConn::rollbackTransaction()
{
    return mysql_rollback(m_conn);
}

void MySQLConn::freeQueryRes()
{
    if(m_query_res != nullptr) {
        mysql_free_result(m_query_res);
        m_query_res = nullptr;
    }
}

void MySQLConn::refreshActiveTime()
{
    // 获得最初建立连接的时间戳
    m_time = std::chrono::steady_clock::now();
}

long long MySQLConn::getConnAliveTime()
{
    // 计算离建立连接的时间间隔
    std::chrono::nanoseconds res = std::chrono::steady_clock::now() - m_time;
    std::chrono::milliseconds ms = std::chrono::duration_cast<std::chrono::milliseconds>(res);
    return ms.count();
}
