cmake_minimum_required(VERSION 3.15)
project(BSwitcher)

set(CMAKE_CXX_STANDARD 17)  #C++17足够了
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT DEFINED ENV{ANDROID_NDK})
    message(FATAL_ERROR "ANDROID_NDK environment variable is not set")
endif()

set(ANDROID_NDK $ENV{ANDROID_NDK})  #ndk
set(CMAKE_CXX_COMPILER ${ANDROID_NDK}/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android29-clang++)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Ofast -flto")

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ===========================nlohmann/json======================
add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE
    lib/json/include
)

# ============================JSONSocket========================= 
add_library(JSONSocket STATIC
    src/JSONSocket/JSONSocket.cpp
    src/JSONSocket/UnixSocketServer/UnixSocketServer.cpp
)

target_include_directories(JSONSocket PUBLIC
    src/JSONSocket
    src/JSONSocket/UnixSocketServer
    src/common
)

target_link_libraries(JSONSocket PRIVATE nlohmann_json)

target_compile_options(JSONSocket PRIVATE -Wall -Wextra -pedantic)

# ===============================主程序=============================
add_executable(${PROJECT_NAME}
    src/main.cpp
    src/ForegroundApp.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE
    src
    src/JSONSocketModule
    src/common
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    log
    nlohmann_json
    JSONSocket
    -static-libstdc++
)

target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -pedantic)

# ============================socket_send=============================
add_executable(socket_send
    tool/dontHaveNc.cpp
)

target_compile_options(socket_send PRIVATE -Wall -Wextra -pedantic)
target_link_libraries(socket_send
    PRIVATE
    -static-libstdc++
)

set_target_properties(socket_send PROPERTIES
    LINK_FLAGS "-static"    # 最大兼容性，静态链接
)

# ===========================module=================================
add_custom_target(copy_module ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/module
        ${CMAKE_BINARY_DIR}/module
    COMMENT "Copying module directory to build directory"
)

# ============================web UI================================
add_custom_target(build_webui ALL
    COMMAND npm install
    COMMAND npm run build
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/webui
    COMMENT "Building web UI"
)

#=========================打包=============================
# 复制文件
add_custom_target(copy_dist ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/webui/dist
        ${CMAKE_BINARY_DIR}/module/webroot
    COMMENT "Copying dist to module/webroot"
    DEPENDS build_webui
)

add_custom_target(copy_binary ALL
    COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_BINARY_DIR}/bin/${PROJECT_NAME}
        ${CMAKE_BINARY_DIR}/module/${PROJECT_NAME}
    COMMENT "Copying main binary to module directory"
    DEPENDS ${PROJECT_NAME}
)

add_custom_target(copy_socket_send ALL
    COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_BINARY_DIR}/bin/socket_send
        ${CMAKE_BINARY_DIR}/module/socket_send
    COMMENT "Copying socket_send tool to module directory"
    DEPENDS socket_send
)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(ZIP_FILENAME "BSwitcher_module_debug.zip")
else()
    set(ZIP_FILENAME "BSwitcher_module_release.zip")
endif()


# 压缩
add_custom_target(create_zip ALL
    COMMAND ${CMAKE_COMMAND} -E tar cf ${CMAKE_BINARY_DIR}/${ZIP_FILENAME} --format=zip .
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/module
    COMMENT "Creating ${ZIP_FILENAME}"
    DEPENDS copy_module copy_dist copy_binary copy_socket_send
)

# 设置依赖关系
add_dependencies(copy_dist copy_module)
add_dependencies(copy_binary copy_module)
add_dependencies(copy_socket_send copy_module)
add_dependencies(create_zip copy_dist copy_binary copy_socket_send)
