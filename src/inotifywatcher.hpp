/*提供基础的inotify阻塞式监听*/
/*不解析内容，能跑就行*/
#ifndef INOTIFY_WATCHER
#define INOTIFY_WATCHER

#include <vector>
#include <string>
#include <sys/inotify.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/time.h>
#include <cstring>
#include <algorithm>
#include <thread>
#include <chrono>

#include "Alog.hpp"

class FileWatcher {
private:
    int inotify_fd;
    std::vector<int> watch_descriptors;
    std::vector<std::string> watched_files;
    bool initialized;

public:
    // 构造函数，接收要监听的文件列表
    FileWatcher(const std::vector<std::string>& file_paths = {}) 
        : inotify_fd(-1), initialized(false) {
        if (!file_paths.empty()) {
            watched_files = file_paths;
        }
    }
    
    // 析构函数，自动清理资源
    ~FileWatcher() {
        cleanup();
    }
    
    // 设置要监听的文件（在initialize之前调用）
    void setFilesToWatch(const std::vector<std::string>& file_paths) {
        if (initialized) {
            LOGW("FileWatcher already initialized, cannot set files");
            return;
        }
        watched_files = file_paths;
    }
    
    // 添加要监听的文件（在initialize之前调用）
    void addFileToWatch(const std::string& file_path) {
        if (initialized) {
            LOGW("FileWatcher already initialized, cannot add file");
            return;
        }
        watched_files.push_back(file_path);
    }

    // 初始化inotify实例并开始监听
    bool initialize() {
        if (inotify_fd >= 0) {
            LOGD("FileWatcher already initialized");
            return true;
        }
        
        if (watched_files.empty()) {
            LOGW("No files to watch specified");
            return false;
        }
        
        inotify_fd = inotify_init1(IN_NONBLOCK); // 使用非阻塞模式
        if (inotify_fd < 0) {
            LOGE("Failed to initialize inotify: %s", strerror(errno));
            return false;
        }
        
        bool at_least_one_valid = false;
        
        for (const auto& file_path : watched_files) {
            int wd = inotify_add_watch(inotify_fd, file_path.c_str(), IN_MODIFY);
            if (wd < 0) {
                LOGW("Failed to watch file: %s, error: %s", file_path.c_str(), strerror(errno));
                continue;
            }
            
            LOGD("Successfully registered inotify for: %s with watch descriptor: %d", 
                 file_path.c_str(), wd);
            watch_descriptors.push_back(wd);
            at_least_one_valid = true;
        }
        
        if (!at_least_one_valid) {
            LOGW("No valid files to watch");
            close(inotify_fd);
            inotify_fd = -1;
            return false;
        }
        
        initialized = true;
        LOGI("Registered inotify for %zu files in total", watch_descriptors.size());
        return true;
    }
    
    // 主要的阻塞函数
    bool wait(int timeout_ms) {
        if (inotify_fd < 0) {
            return false;   // 未启用inotify时会在这里返回
        }
        
        if (watch_descriptors.empty()) {
            LOGW("No valid watch descriptors available");
            return false;
        }
        
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(inotify_fd, &read_fds);
        
        struct timeval timeout;
        struct timeval* timeout_ptr = nullptr;
        
        if (timeout_ms >= 0) {
            timeout.tv_sec = timeout_ms / 1000;
            timeout.tv_usec = (timeout_ms % 1000) * 1000;
            timeout_ptr = &timeout;
            LOGD("Waiting for file modification events with timeout: %d ms", timeout_ms);
        } else {
            LOGD("Waiting for file modification events indefinitely");
        }
        
        int result = select(inotify_fd + 1, &read_fds, nullptr, nullptr, timeout_ptr);  // 不出意外会在这里阻塞

        if (result < 0) {
            if (errno == EINTR) {
                LOGD("select() interrupted by signal");
                return true;
            }
            LOGE("select() failed: %s", strerror(errno));
            return false;
        } else if (result == 0) {
            LOGD("Wait timeout reached");
            return true;
        }
        
        if (FD_ISSET(inotify_fd, &read_fds)) {  
            std::this_thread::sleep_for(std::chrono::milliseconds(5));  // 等待5ms,确保同时触发的事件到齐
            if (clearEvents()) {    // 清理
                LOGD("File modification detected and events cleared");
                return true;
            } else {
                LOGE("Failed to clear events");
                return false;
            }
        }
        
        LOGW("Unexpected: select returned but inotify fd not set");
        return true;
    }
    
    // 重新初始化
    bool reinitialize(const std::vector<std::string>& new_file_paths = {}) {
        cleanup();
        
        if (!new_file_paths.empty()) {
            watched_files = new_file_paths;
        }
        
        return initialize();
    }

    // 清理资源
    void cleanup() {
        if (inotify_fd < 0 && watch_descriptors.empty()) {
            return; // 已经清理过了
        }
        
        LOGI("Cleaning up FileWatcher resources");
        
        for (int wd : watch_descriptors) {
            if (inotify_rm_watch(inotify_fd, wd) < 0) {
                LOGW("Failed to remove watch descriptor %d: %s", wd, strerror(errno));
            } else {
                LOGD("Removed watch descriptor: %d", wd);
            }
        }
        watch_descriptors.clear();
        
        if (inotify_fd >= 0) {
            close(inotify_fd);
            inotify_fd = -1;
            LOGD("Closed inotify file descriptor");
        }
        
        initialized = false;
        LOGI("FileWatcher cleanup completed");
    }
    
    // 获取当前监听的文件数量
    size_t getWatchedFileCount() const {
        return watch_descriptors.size();
    }
    
    // 获取配置的要监听的文件数量
    size_t getConfiguredFileCount() const {
        return watched_files.size();
    }
    
    // 检查是否已初始化
    bool isInitialized() const {
        return initialized && inotify_fd >= 0 && !watch_descriptors.empty();
    }
    
    // 获取监听的文件列表
    const std::vector<std::string>& getWatchedFiles() const {
        return watched_files;
    }

private:
    // 禁用拷贝构造和赋值操作
    FileWatcher(const FileWatcher&) = delete;
    FileWatcher& operator=(const FileWatcher&) = delete;

    bool clearEvents() { // 不关心发生了什么，总之发生过就行
        const size_t buffer_size = 1024;
        char buffer[buffer_size];
        
        ssize_t total_read = 0;
        ssize_t length;
        
        while ((length = read(inotify_fd, buffer, buffer_size)) > 0) {  // 清理事件
            total_read += length;
            
            char* ptr = buffer;
            while (ptr < buffer + length) {
                struct inotify_event* event = reinterpret_cast<struct inotify_event*>(ptr);
                
                if (event->mask & IN_MODIFY) {  // 调试用
                    LOGD("File modification event detected for watch descriptor: %d", event->wd);
                }
                
                ptr += sizeof(struct inotify_event) + event->len;
            }
        }
        
        if (length < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                LOGD("Cleared %zd bytes of inotify events (%zu events approx)", 
                     total_read, total_read / (sizeof(struct inotify_event) + 16));
                return true;
            } else {
                LOGE("Error reading inotify events: %s", strerror(errno));
                return false;
            }
        }
        
        LOGD("Cleared %zd bytes of inotify events", total_read);
        return true;
    }
};

#endif