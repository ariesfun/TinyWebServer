cmake_minimum_required(VERSION 3.0)
project(TinyWebServer)

set(CMAKE_CXX_STANDARD 14)

# 添加头文件
include_directories(./include/log)

# 添加源文件
aux_source_directory(./src/log DIR_SRCS)

add_executable(TinyWebServer main.cpp ${DIR_SRCS})
