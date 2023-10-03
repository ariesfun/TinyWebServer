cmake_minimum_required(VERSION 3.10)
project(TinyWebServer)

set(CMAKE_CXX_STANDARD 14)

# 添加头文件
include_directories(./include/log ./include/timer ./include/pool ./include/http ./include/server)

# 添加各个模块的源文件
aux_source_directory(./src/log DIR_SRCS)
aux_source_directory(./src/timer TIMER_SRCS)
aux_source_directory(./src/pool POOL_SRCS)
aux_source_directory(./src/http HTTP_SRCS)
aux_source_directory(./src/server SERVER_SRCS)

add_executable(TinyWebServer main.cpp ${DIR_SRCS} ${TIMER_SRCS} ${POOL_SRCS} ${HTTP_SRCS} ${SERVER_SRCS})

# 添加链接库
target_link_libraries(TinyWebServer pthread mysqlclient jsoncpp curl)
