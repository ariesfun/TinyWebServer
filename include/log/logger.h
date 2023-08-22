#ifndef TINYWEBSERVER_LOGGER_H
#define TINYWEBSERVER_LOGGER_H

#include <string>
#include <fstream>
#include <mutex>

namespace ariesfun{
namespace log{

#define Debug(format, ...) \
    Logger::getInstance()->log_write(Logger::DEBUG, __FILE__, __LINE__, format, ##__VA_ARGS__);

#define Info(format, ...) \
    Logger::getInstance()->log_write(Logger::INFO, __FILE__, __LINE__, format, ##__VA_ARGS__);

#define Warn(format, ...) \
    Logger::getInstance()->log_write(Logger::WARN, __FILE__, __LINE__, format, ##__VA_ARGS__);

#define Error(format, ...) \
    Logger::getInstance()->log_write(Logger::ERROR, __FILE__, __LINE__, format, ##__VA_ARGS__);

#define Fatal(format, ...) \
    Logger::getInstance()->log_write(Logger::FATAL, __FILE__, __LINE__, format, ##__VA_ARGS__);

class Logger {
public:
    enum Level
    {
        DEBUG = 0,
        INFO,
        WARN,
        ERROR,
        FATAL,
        LEVEL_COUNT // 设置5个级别，并记录数量为5
    };

    static Logger* getInstance(); // 定义成单例类（懒汉式）

    void open(const std::string &filename);
    void close();

    void log_write(Level level, const char* file, int line, const char* format, ...); // 写入日志信息的操作
    void log_setlevel(Level level);
    void log_maxsize(int bytes);

private:
    Logger();
    ~Logger();
    Logger(const Logger &) = delete;
    Logger& operator = (const Logger &) = delete;

    void log_rotate(); // 日志翻滚

private:
    std::string m_filename;
    std::ofstream m_fout; // 写入文件的输出流
    std::mutex m_mutex; // 添加互斥锁以进行同步
    static const char* level_str[LEVEL_COUNT];

    Level m_level; // 当前日志级别
    int m_cursize; // 文件字节大小
    int m_maxsize;
};

}}

#endif