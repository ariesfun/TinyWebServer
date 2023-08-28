cmake_minimum_required(VERSION 3.5)
project(TinyWebServer)

set(CMAKE_CXX_STANDARD 14)

# 添加头文件
include_directories(./include/log ./include/pool ./include/http)

# 添加各个模块的源文件
aux_source_directory(./src/log DIR_SRCS)
aux_source_directory(./src/pool POOL_SRCS)
aux_source_directory(./src/http HTTP_SRCS)

add_executable(TinyWebServer main.cpp ${DIR_SRCS} ${POOL_SRCS} ${HTTP_SRCS})
