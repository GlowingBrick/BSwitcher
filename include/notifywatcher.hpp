
#include <vector>
#include <string>
#include <sys/inotify.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/time.h>
#include <cstring>
#include <algorithm>

#include "Alog.hpp"


class FileWatcher {
private:
    static int inotify_fd;
    static std::vector<int> watch_descriptors;
    static int timeout_ms;

public:
    // 初始化inotify实例
    static bool initialize(const std::vector<std::string>& file_paths, int timeout_ms_param) {
        timeout_ms = timeout_ms_param;
        
        if (inotify_fd >= 0) {
            cleanup();
        }
        
        inotify_fd = inotify_init1(IN_NONBLOCK); // 使用非阻塞模式
        if (inotify_fd < 0) {
            LOGE("Failed to initialize inotify: %s", strerror(errno));
            return false;
        }
        
        LOGI("FileWatcher initialized with timeout: %d ms", timeout_ms);
        
        bool at_least_one_valid = false;
        
        for (const auto& file_path : file_paths) {
            int wd = inotify_add_watch(inotify_fd, file_path.c_str(), IN_MODIFY);
            if (wd < 0) {
                LOGW("Failed to watch file: %s, error: %s", file_path.c_str(), strerror(errno));
                continue;
            }
            
            LOGD("Successfully watching file: %s with watch descriptor: %d", file_path.c_str(), wd);
            watch_descriptors.push_back(wd);
            at_least_one_valid = true;
        }
        
        if (!at_least_one_valid) {
            LOGW("No valid files to watch among the provided paths");
            close(inotify_fd);
            inotify_fd = -1;
            return false;
        }
        
        LOGI("FileWatcher successfully watching %zu file(s)", watch_descriptors.size());
        return true;
    }
    
    static bool wait() {    //主要的阻塞函数
        if (inotify_fd < 0) {
            LOGE("FileWatcher not initialized");
            return false;
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
        
        int result = select(inotify_fd + 1, &read_fds, nullptr, nullptr, timeout_ptr);  //不出意外会在这里阻塞
        
        if (result < 0) {
            if (errno == EINTR) {
                LOGD("select() interrupted by signal");
                return true;
            }
            LOGE("select() failed: %s", strerror(errno));
            return false;
        } else if (result == 0) {
            LOGD("Wait timeout reached");
            return true; // 超时是正常情况
        }
        
        if (FD_ISSET(inotify_fd, &read_fds)) {
            if (clearEvents()) {
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
    

    static void cleanup() {    // 清理资源
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
        
        LOGI("FileWatcher cleanup completed");
    }
    
    static size_t getWatchedFileCount() {
        return watch_descriptors.size();
    }
    
    static bool isInitialized() {
        return inotify_fd >= 0 && !watch_descriptors.empty();
    }

private:

    static bool clearEvents() {
        const size_t buffer_size = 1024;
        char buffer[buffer_size];
        
        ssize_t total_read = 0;
        ssize_t length;
        
        while ((length = read(inotify_fd, buffer, buffer_size)) > 0) {  //清理事件
            total_read += length;
            
            char* ptr = buffer;
            while (ptr < buffer + length) {
                struct inotify_event* event = reinterpret_cast<struct inotify_event*>(ptr);
                
                if (event->mask & IN_MODIFY) {  //调试用
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

// 静态成员初始化
int FileWatcher::inotify_fd = -1;
std::vector<int> FileWatcher::watch_descriptors;
int FileWatcher::timeout_ms = -1;