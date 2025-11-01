cmake_minimum_required(VERSION 3.15)
project(BSwitcher)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT DEFINED ENV{ANDROID_NDK})
    message(FATAL_ERROR "ANDROID_NDK environment variable is not set")
endif()

set(ANDROID_NDK $ENV{ANDROID_NDK})

# 设置交叉编译工具链
set(CMAKE_CXX_COMPILER ${ANDROID_NDK}/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android29-clang++)


# 设置编译模式（Debug/Release）
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Ofast -flto")

#add_compile_definitions(
#    PRINTLOG
#)

# 设置输出目录
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# 包含目录
include_directories(include)
include_directories(lib)


# 添加nlohmann/json库
add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/json/include
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/json
)


# 添加可执行文件
add_executable(${PROJECT_NAME}
    src/main.cpp
    src/UnixSocketServer.cpp
    src/JSONSocket.cpp
    src/ForegroundApp.cpp
)

# 链接库
target_link_libraries(${PROJECT_NAME} 
    PRIVATE log
    nlohmann_json
)


target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -pedantic)

