#include <iostream>
#include "logger.h"

using namespace ariesfun::log;

int main()
{
    Logger::getInstance()->open("../log_info/LOG_INFO.log");
    Logger::getInstance()->log_setlevel(Logger::INFO);
    Logger::getInstance()->log_maxsize(2000);

    // 日志类具体用法示例
//    Debug("hello world");
//    Debug("name is %s, age is %d", "jack", 18);
//    Info("info message");
//    Warn("warning message");
//    Error("error message");
//    Fatal("fatal massgae");





    return 0;
}
