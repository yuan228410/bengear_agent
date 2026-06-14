cmake_minimum_required(VERSION 3.20)
project(webserver CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 优化选项
set(CMAKE_BUILD_TYPE Release)
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -DNDEBUG -flto")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-flto")

# 地址消毒（debug模式可选）
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g -fsanitize=address -fno-omit-frame-pointer")

# 头文件路径
include_directories(${CMAKE_SOURCE_DIR}/include)

# 源文件列表
file(GLOB_RECURSE SOURCES src/*.cpp)

# 可执行文件
add_executable(webserver ${SOURCES})
target_link_libraries(webserver pthread)

# 测试目标
add_executable(webserver_test src/main.cpp)
target_link_libraries(webserver_test pthread)
