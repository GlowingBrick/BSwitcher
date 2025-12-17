/* inotify 监听器。只管发生不管内容 */
#ifndef INOTIFY_WATCHER
#define INOTIFY_WATCHER

#include "Alog.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <string>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <thread>
#include <unistd.h>
#include <vector>

class FileWatcher {
private:
    std::atomic<bool> running_{false};
    std::thread monitor_thread_;

    std::mutex event_mutex_;
    std::condition_variable event_cv_;
    std::atomic<bool> event_pending_{true};

    int epoll_fd{-1};
    int inotify_fd{-1};
    int wakeup_fd{-1};
    int sleeptime_ms_;
    uint32_t event_mask_;
    std::vector<int> watch_descriptors;
    std::vector<std::string> watched_files;
    bool initialized{false};

    static const int MAX_EVENTS = 32;

public:
    FileWatcher(const std::vector<std::string>& file_paths = {},
                int sleeptime_ms = 100,
                uint32_t event_mask = IN_MODIFY)  //默认监听修改事件
        : sleeptime_ms_(sleeptime_ms), event_mask_(event_mask) {

        if (!file_paths.empty()) {
            watched_files = file_paths;
        }

        wakeup_fd = eventfd(0, EFD_NONBLOCK);
        if (wakeup_fd < 0) {
            LOGE("Failed to create eventfd: %s", strerror(errno));
        }
    }

    ~FileWatcher() {
        stop();
        cleanup();
    }

    void setFilesToWatch(const std::vector<std::string>& file_paths) {
        if (initialized) {
            LOGW("Cannot define while the monitoring thread is running.");
            return;
        }
        watched_files = file_paths;
    }

    void addFileToWatch(const std::string& file_path) {
        if (initialized) {
            LOGW("Cannot define while the monitoring thread is running.");
            return;
        }
        watched_files.push_back(file_path);
    }

    void setEventMask(uint32_t event_mask) {
        if (initialized) {
            LOGW("Cannot change event mask while the monitoring thread is running.");
            return;
        }
        event_mask_ = event_mask;
        LOGD("Event mask set to: 0x%08X", event_mask_);
    }

    void addEventType(uint32_t event_type) {
        if (initialized) {
            LOGW("Cannot change event mask while the monitoring thread is running.");
            return;
        }
        event_mask_ |= event_type;
        LOGD("Added event type: 0x%08X, current mask: 0x%08X", event_type, event_mask_);
    }

    void removeEventType(uint32_t event_type) {
        if (initialized) {
            LOGW("Cannot change event mask while the monitoring thread is running.");
            return;
        }
        event_mask_ &= ~event_type;
        LOGD("Removed event type: 0x%08X, current mask: 0x%08X", event_type, event_mask_);
    }

    uint32_t getEventMask() const {
        return event_mask_;
    }

    std::string getEventMaskDescription() const {
        std::vector<std::string> events;

        if (event_mask_ & IN_ACCESS) events.push_back("IN_ACCESS");
        if (event_mask_ & IN_MODIFY) events.push_back("IN_MODIFY");
        if (event_mask_ & IN_ATTRIB) events.push_back("IN_ATTRIB");
        if (event_mask_ & IN_CLOSE_WRITE) events.push_back("IN_CLOSE_WRITE");
        if (event_mask_ & IN_CLOSE_NOWRITE) events.push_back("IN_CLOSE_NOWRITE");
        if (event_mask_ & IN_OPEN) events.push_back("IN_OPEN");
        if (event_mask_ & IN_MOVED_FROM) events.push_back("IN_MOVED_FROM");
        if (event_mask_ & IN_MOVED_TO) events.push_back("IN_MOVED_TO");
        if (event_mask_ & IN_CREATE) events.push_back("IN_CREATE");
        if (event_mask_ & IN_DELETE) events.push_back("IN_DELETE");
        if (event_mask_ & IN_DELETE_SELF) events.push_back("IN_DELETE_SELF");
        if (event_mask_ & IN_MOVE_SELF) events.push_back("IN_MOVE_SELF");

        if (events.empty()) {
            return "None";
        }

        std::string result;
        for (size_t i = 0; i < events.size(); ++i) {
            if (i > 0) result += " | ";
            result += events[i];
        }
        return result;
    }

    bool initialize() {
        if (initialized) {
            LOGD("FileWatcher already initialized");
            return true;
        }

        if (watched_files.empty()) {
            LOGW("No files to watch specified");
            return false;
        }

        if (event_mask_ == 0) {
            LOGW("No event types specified, using default IN_MODIFY");
            event_mask_ = IN_MODIFY;
        }

        if (wakeup_fd < 0) {
            wakeup_fd = eventfd(0, EFD_NONBLOCK);
            if (wakeup_fd < 0) {
                LOGE("Failed to create eventfd: %s", strerror(errno));
                return false;
            }
        }

        epoll_fd = epoll_create1(0);
        if (epoll_fd < 0) {
            LOGE("Failed to create epoll instance: %s", strerror(errno));
            return false;
        }

        inotify_fd = inotify_init1(IN_NONBLOCK);
        if (inotify_fd < 0) {
            LOGE("Failed to initialize inotify: %s", strerror(errno));
            close(epoll_fd);
            epoll_fd = -1;
            return false;
        }

        struct epoll_event event;
        event.events = EPOLLIN;
        event.data.fd = inotify_fd;
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, inotify_fd, &event) < 0) {
            LOGE("Failed to add inotify fd: %s", strerror(errno));
            close(inotify_fd);
            close(epoll_fd);
            inotify_fd = -1;
            epoll_fd = -1;
            return false;
        }

        event.events = EPOLLIN;
        event.data.fd = wakeup_fd;
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, wakeup_fd, &event) < 0) {
            LOGE("Failed to add wakeup fd to epoll: %s", strerror(errno));
            close(inotify_fd);
            close(epoll_fd);
            inotify_fd = -1;
            epoll_fd = -1;
            return false;
        }

        bool at_least_one_valid = false;
        for (const auto& file_path : watched_files) {
            int wd = inotify_add_watch(inotify_fd, file_path.c_str(), event_mask_);
            if (wd < 0) {
                LOGW("Failed to watch file: %s, error: %s", file_path.c_str(), strerror(errno));
                continue;
            }

            LOGD("Successfully registered inotify for: %s with watch descriptor: %d, events: 0x%08X",
                 file_path.c_str(), wd, event_mask_);
            watch_descriptors.push_back(wd);
            at_least_one_valid = true;
        }

        if (!at_least_one_valid) {
            LOGW("No valid files to watch");
            cleanup();
            return false;
        }

        initialized = true;
        start_monitor_thread();

        LOGI("Registered inotify for %zu files with event mask: %s",
             watch_descriptors.size(), getEventMaskDescription().c_str());
        return true;
    }

    bool reinitialize(const std::vector<std::string>& new_file_paths = {},
                      uint32_t new_event_mask = 0) {
        stop();
        cleanup();

        if (!new_file_paths.empty()) {
            watched_files = new_file_paths;
        }

        if (new_event_mask != 0) {
            event_mask_ = new_event_mask;
        }

        return initialize();
    }

    bool wait(int timeout_ms = -1, int delay_clean_ms = 0) {  //主等待函数
        if (!initialized || !running_) {
            return false;
        }

        bool has_event = false;
        {
            std::unique_lock<std::mutex> lock(event_mutex_);

            if (timeout_ms > 0) {
                has_event = event_cv_.wait_for(lock,
                                               std::chrono::milliseconds(timeout_ms),
                                               [this]() { return event_pending_.load(std::memory_order_relaxed); });
            } else {
                event_cv_.wait(lock, [this]() { return event_pending_.load(std::memory_order_relaxed); });
                has_event = true;
            }
        }

        if (!has_event) {
            return false;
        }

        if (delay_clean_ms > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_clean_ms));
        }

        event_pending_.store(false,std::memory_order_relaxed);

        return true;
    }

    void cleanup() {
        if (!watch_descriptors.empty()) {
            LOGI("Cleaning up FileWatcher resources");

            for (int wd : watch_descriptors) {
                if (inotify_fd >= 0 && inotify_rm_watch(inotify_fd, wd) < 0) {
                    LOGW("Failed to remove watch descriptor %d: %s", wd, strerror(errno));
                } else {
                    LOGD("Removed watch descriptor: %d", wd);
                }
            }
            watch_descriptors.clear();
        }

        stop(); //关闭监控

        if (epoll_fd >= 0) {
            close(epoll_fd);
            epoll_fd = -1;
        }

        if (inotify_fd >= 0) {
            close(inotify_fd);
            inotify_fd = -1;
        }

        if (wakeup_fd >= 0) {
            close(wakeup_fd);
            wakeup_fd = -1;
        }

        initialized = false;
        LOGI("FileWatcher cleanup completed");
    }

    size_t getWatchedFileCount() const {
        return watch_descriptors.size();
    }

    size_t getConfiguredFileCount() const {
        return watched_files.size();
    }

    bool isInitialized() const {
        return initialized && running_;
    }

    const std::vector<std::string>& getWatchedFiles() const {
        return watched_files;
    }

private:
    FileWatcher(const FileWatcher&) = delete;
    FileWatcher& operator=(const FileWatcher&) = delete;

    void stop() {
        if (running_.exchange(false)) {
            LOGD("Stopping FileWatcher monitor thread...");

            if (wakeup_fd >= 0) {
                uint64_t value = 1;
                ssize_t result = write(wakeup_fd, &value, sizeof(value));
                if (result < 0) {
                    LOGW("Failed to write to wakeup_fd: %s", strerror(errno));
                }
            }

            {
                std::lock_guard<std::mutex> lock(event_mutex_);
                event_pending_.store(true,std::memory_order_relaxed);  //确保wait不会无限阻塞
            }
            event_cv_.notify_one();

            if (monitor_thread_.joinable()) {
                monitor_thread_.join();
                LOGD("FileWatcher monitor thread stopped");
            }
        }
    }

    void start_monitor_thread() {
        running_.store(true);
        monitor_thread_ = std::thread(&FileWatcher::monitor_loop, this);
    }

    void monitor_loop() {
        LOGD("FileWatcher monitor thread started");
        const size_t buffer_size = 4096;
        char buffer[buffer_size];
        struct epoll_event events[MAX_EVENTS];

        sigset_t mask;
        sigfillset(&mask);
        pthread_sigmask(SIG_SETMASK, &mask, nullptr);

        event_pending_.store(false,std::memory_order_relaxed);

        while (running_.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(sleeptime_ms_));

            int num_events = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);

            if (!running_.load()) {
                uint64_t value;
                ssize_t result = read(wakeup_fd, &value, sizeof(value));
                LOGD("inotify terminated");
                break;
            }

            if (num_events < 0) {
                if (errno == EINTR) {
                    LOGD("epoll_wait interrupted by signal");
                    continue;
                }
                if (errno == EBADF) {
                    LOGD("epoll terminated abnormally, descriptor invalidated.");
                    break;
                }
                LOGE("epoll_wait() failed in monitor thread: %s", strerror(errno));
                break;
            }

            if (num_events == 0) {
                LOGW("epoll_wait returned 0 events unexpectedly");
                continue;
            }

            bool notify_main_thread = false;
            bool should_exit = false;

            for (int i = 0; i < num_events; ++i) {
                int ready_fd = events[i].data.fd;

                if (ready_fd == wakeup_fd) {
                    uint64_t value;
                    ssize_t result = read(wakeup_fd, &value, sizeof(value));
                    break;
                } else if (ready_fd == inotify_fd && (events[i].events & EPOLLIN)) {
                    ssize_t total_read = 0;
                    ssize_t length;

                    while ((length = read(inotify_fd, buffer, buffer_size)) > 0) {
                        total_read += length;
                    }

                    if (length < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EBADF) {
                        LOGE("Error reading inotify events: %s", strerror(errno));
                        should_exit = true;
                        break;
                    }

                    if (total_read > 0) {
                        notify_main_thread = true;
                    }
                }
            }

            if (should_exit) {
                break;
            }

            if (notify_main_thread && running_.load()) {
                {
                    std::lock_guard<std::mutex> lock(event_mutex_);
                    event_pending_.store(true,std::memory_order_relaxed);   // 触发事件
                }
                event_cv_.notify_one();
            }
        }

        LOGD("FileWatcher monitor thread exited");
    }
};

#endif