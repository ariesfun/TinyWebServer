#include "logger.h"
#include <cstring>
#include <iostream>
#include <cstdarg>

using namespace ariesfun::log;

Logger::Logger() : m_level(DEBUG), m_cursize(0), m_maxsize(0) {} // 默认DEBUG级别

Logger::~Logger() {
    close();
}

Logger* Logger::getInstance()
{
    static Logger instance; // 使用静态局部变量，保证线程安全
    return &instance;
}

const char* Logger::level_str[LEVEL_COUNT] =
{
     "DEBUG", "INFO", "WARN", "ERROR", "FATAL"
};

void Logger::open(const std::string &filename) {
    m_filename = filename;
    m_fout.open(filename, std::ios::app);  // 以追加方式打开文件，std::ios::app表示在文件末尾添加数据，而不覆盖已有内容
    if(m_fout.fail()) {
        throw std::logic_error("open local log file failed: " + filename);
    }
    m_fout.seekp(0, std::ios::end); // 获取当前文件的长度（即当前文件指针的位置），并将其保存到成员变量m_curlen中
    m_cursize = m_fout.tellp();
}

void Logger::close() {
    m_fout.close();
}

void Logger::log_write(Level level, const char* file, int line, const char* format, ...) {
    // 避免多线程下的死锁问题
    std::lock_guard<std::mutex> lock(m_mutex); // 写入文件流之前获取锁，互斥锁在作用域结束时自动释放
    if(m_level > level) return; // 只记录当前级别以上的日志记录
    if(m_fout.fail()) {
        throw std::logic_error("write log file failed: " + m_filename);
    }

    // TODO
    // localtime_r()函数是可重入的，它接受一个额外的tm结构指针，以避免静态缓冲区的冲突
    // 多线程下的安全问题？？？待测试
    time_t ticks = time(nullptr);
    struct tm tm_result;
    struct tm* ptm = localtime_r(&ticks, &tm_result);
    char timestamp[32];
    if(ptm != nullptr) {
        memset(timestamp, 0, sizeof(timestamp));
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", ptm);
    }
    else {
        throw std::logic_error("get localtime failed");
    }
    const char* ftm = "%s [%s] %s:%d ";
    int size = snprintf(nullptr, 0, ftm, timestamp, level_str[level], file, line);
    if(size > 0) { // size是ftm串的长度
        char* buffer = new char[size + 1];
        snprintf(buffer, size + 1, ftm, timestamp, level_str[level], file, line);
        buffer[size] = '\0';
        m_fout << buffer;
        m_cursize += size;
        delete[] buffer;
    }

    // start、end用于处理可变参数的宏,处理可变参数列表arg_ptr，这些参数将被格式化进format字符串中
    va_list arg_ptr;
    va_start(arg_ptr, format);
    size = vsnprintf(nullptr, 0, format, arg_ptr);
    va_end(arg_ptr);
    if(size > 0) {
        char* arg_content = new char[size + 1];
        va_start(arg_ptr, format);
        vsnprintf(arg_content, size + 1, format, arg_ptr);
        va_end(arg_ptr);
        m_fout << arg_content;
        m_cursize += size;
        delete[] arg_content;
    }
    m_fout << "\n";
    m_fout.flush();
    if(m_cursize >= m_maxsize && m_maxsize > 0) { // 超过单个文件限制，进行日志翻滚（自动备份）
        log_rotate();
    }
}

void Logger::log_setlevel(Level level) {
    m_level = level;
}

void Logger::log_maxsize(int bytes) {
    m_maxsize = bytes;
}

void Logger::log_rotate() {
    close();
    time_t ticks = time(nullptr);
    struct tm tm_result;
    struct tm* ptm = localtime_r(&ticks, &tm_result);
    char timestamp[32];
    if(ptm != nullptr) {
        memset(timestamp, 0, sizeof(timestamp));
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d-%H-%M-%S", ptm); // 注意win环境':'会出问题
    }
    else {
        throw std::logic_error("get localtime failed");
    }
    std::string filename = m_filename + "-" + timestamp;
    if(rename(m_filename.c_str(), filename.c_str()) != 0) { // 以时间戳来命名新的日志备份文件
        throw std::logic_error("rename backup log file failed: " + std::string(strerror(errno)));
    }
    open(m_filename); // 重新打开一个新日志文件，继续记录
}