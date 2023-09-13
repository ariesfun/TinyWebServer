#include "logger.h"
#include <cstring>
#include <iostream>
#include <cstdarg>
#include <future>

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

std::string Logger::getDatewithTime() { // 用于获取更详细的时间信息
    auto now = std::chrono::system_clock::now();
    // 通过计算当前时间点相对于UNIX时间1970101的时间差，来获得当前时间的毫秒部分
    uint64_t dis_millseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count()
                               -
                               std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count() * 1000;
    time_t tt = std::chrono::system_clock::to_time_t(now);
    // localtime_r()函数是可重入的，它接受一个额外的tm结构指针，以避免静态缓冲区的冲突
    struct tm tm_result;
    auto time_tm = localtime_r(&tt, &tm_result);
    char strTimeStamp[128] = {0};
    if(time_tm != nullptr) {
        sprintf(strTimeStamp, "%d-%02d-%02d %02d:%02d:%02d:%03d", time_tm->tm_year +1900, // 毫秒数不足三位前面补零
                time_tm->tm_mon + 1, time_tm->tm_mday, time_tm->tm_hour,
                time_tm->tm_min, time_tm->tm_sec, (int)dis_millseconds);
    }
    else {
        throw std::logic_error("get detail localtime failed");
    }

    return strTimeStamp;
}

unsigned long Logger::getThreadId() {
    std::hash<std::thread::id> hash_thread_number; // 使用无符号整数类型可以更好地反映哈希值的本质特性，即非负性
    return hash_thread_number(std::this_thread::get_id());
}

void Logger::log_write(Level level, const char* file, int line, const char* func, const char* format, ...) {
    // 避免多线程下的死锁问题, 使用前加一把大锁
    std::lock_guard<std::mutex> lock(m_mutex); // 写入文件流之前获取锁，互斥锁在作用域结束时自动释放
    if(m_level > level) return; // 只记录当前级别以上的日志记录
    if(m_fout.fail()) {
        throw std::logic_error("write log file failed: " + m_filename);
    }
    const std::string timestamp = getDatewithTime();
    char ftm[] = "[%s] [%s] [%s:%d] (func:%s) <thread-id:%lu> ";
    const char* file_name = strrchr(file, '/');
    if(file_name) {
        ++file_name;
    }
    int size = snprintf(nullptr, 0, ftm, timestamp.c_str(), level_str[level], file_name, line, func, getThreadId());
    if(size > 0) { // size是ftm串的长度
        char* buffer = new char[size + 1];
        snprintf(buffer, size + 1, ftm, timestamp.c_str(), level_str[level], file_name, line, func, getThreadId());
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
