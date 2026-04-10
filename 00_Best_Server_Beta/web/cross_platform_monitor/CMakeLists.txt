cmake_minimum_required(VERSION 3.10)
project(SystemMonitorTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 创建可执行文件
add_executable(system_monitor_test system_monitor_test.cpp)