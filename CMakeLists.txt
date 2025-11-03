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
    set(CMAKE_BUILD_TYPE Release)
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
include_directories(
    include
    include/JSONSocketModule
    )
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

# 复制module文件夹到构建目录
add_custom_target(copy_module ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/module
        ${CMAKE_BINARY_DIR}/module
    COMMENT "Copying module directory to build directory"
)

# 构建web UI
add_custom_target(build_webui ALL
    COMMAND npm install
    COMMAND npm run build
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/webui
    COMMENT "Building web UI"
)

# 复制dist到module/webroot
add_custom_target(copy_dist ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/webui/dist
        ${CMAKE_BINARY_DIR}/module/webroot
    COMMENT "Copying dist to module/webroot"
    DEPENDS build_webui
)

# 复制二进制文件到module目录
add_custom_target(copy_binary ALL
    COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_BINARY_DIR}/bin/${PROJECT_NAME}
        ${CMAKE_BINARY_DIR}/module/${PROJECT_NAME}
    COMMENT "Copying binary to module directory"
    DEPENDS ${PROJECT_NAME}
)

# 打包
add_custom_target(create_zip ALL
    COMMAND ${CMAKE_COMMAND} -E tar cf ${CMAKE_BINARY_DIR}/BSwitcher_module.zip --format=zip .
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/module
    COMMENT "Creating BSwitcher_module.zip"
    DEPENDS copy_module copy_dist copy_binary
)

add_dependencies(copy_dist copy_module)
add_dependencies(copy_binary copy_module)
add_dependencies(create_zip copy_dist copy_binary)
