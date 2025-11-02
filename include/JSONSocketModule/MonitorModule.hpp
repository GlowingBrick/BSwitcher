#ifndef POWERMONITORTARGET_HPP
#define POWERMONITORTARGET_HPP

#include "JSONSocket.hpp"
#include <nlohmann/json.hpp>
#include <unordered_map>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <ctime>

// 应用功耗记录结构体
struct AppPower {
    float time_sec = 0.0f;    // 应用运行时间（秒）
    float power_joules = 0.0f; // 焦耳累计
};

class PowerMonitorTarget : public ConfigTarget {
private:
    // 功耗数据存储
    std::unordered_map<std::string, AppPower> app_power_map_;
    mutable std::mutex data_mutex_;
    
    // 传感器文件描述符
    int current_fd_ = -1;
    int voltage_fd_ = -1;
    int status_fd_ = -1;
    
    // 线程控制
    std::atomic<bool> running_{false};
    std::thread worker_thread_;
    std::mutex control_mutex_;
    std::condition_variable cv_;
    
    std::atomic<std::string*> current_app_ptr_{nullptr};
    std::atomic<bool> screen_on_{true};
    std::string current_app_storage_;

    // 初始化传感器
    bool init_power_sensors() {
        current_fd_ = open("/sys/class/power_supply/battery/current_now", O_RDONLY | O_CLOEXEC);
        voltage_fd_ = open("/sys/class/power_supply/battery/voltage_now", O_RDONLY | O_CLOEXEC);
        status_fd_ = open("/sys/class/power_supply/battery/status", O_RDONLY | O_CLOEXEC);
        return (current_fd_ >= 0) && (voltage_fd_ >= 0) && (status_fd_ >= 0);
    }

    // 读取当前功率（瓦特）
    float read_current_power_w() {
        char buf[32] = {0};
        

        if (pread(current_fd_, buf, sizeof(buf)-1, 0) <= 0) return 0.0f;
        long current_ua = atol(buf);
        if (current_ua <= 0) {
            return 0.0f;
        }
        
        memset(buf, 0, sizeof(buf));
        if (pread(voltage_fd_, buf, sizeof(buf)-1, 0) <= 0) return 0.0f;
        long voltage_uv = atol(buf);
        
        // P = (I * V) / 1e12
        return static_cast<float>(static_cast<double>(current_ua) * static_cast<double>(voltage_uv) * 1e-12f);
    }

    // 工作线程
    void worker_loop() {
        if (!init_power_sensors()) {
            return;
        }

        char battery_status;
        float power_w;
        float delta_t;
        
        timespec current_time;
        timespec last_time;
        clock_gettime(CLOCK_MONOTONIC, &last_time);

        while (running_.load(std::memory_order_relaxed)) {

            {
                std::unique_lock<std::mutex> lock(control_mutex_);
                cv_.wait_for(lock, std::chrono::seconds(1), [this]() {
                    return !running_.load(std::memory_order_relaxed);
                });
                
                if (!running_.load(std::memory_order_relaxed)) {
                    break;
                }
            }


            std::string app_name;
            bool screen_status;
            getCurrentState(app_name, screen_status);  

            if(!screen_status || app_name.empty()){ //空白、熄屏时不统计
                clock_gettime(CLOCK_MONOTONIC, &last_time);
                continue;
            }

            if (pread(status_fd_, &battery_status, 1, 0) <= 0) {
                clock_gettime(CLOCK_MONOTONIC, &last_time); //跳过时重置时间
                continue;
            }

            if (battery_status == 'C') {    //充电时不统计
                clock_gettime(CLOCK_MONOTONIC, &last_time);
                continue;
            }

            power_w = read_current_power_w();
            if (power_w <= 0.0f) {
                clock_gettime(CLOCK_MONOTONIC, &current_time);
                continue;
            }

            clock_gettime(CLOCK_MONOTONIC, &current_time);
            delta_t = static_cast<float>(current_time.tv_sec - last_time.tv_sec) +
                     static_cast<float>(static_cast<double>(current_time.tv_nsec - last_time.tv_nsec) * 1e-9);  //计算时间间隔
            last_time = current_time;

            {
                std::lock_guard<std::mutex> lock(data_mutex_);  //填入内存
                AppPower& stats = app_power_map_[app_name];
                stats.time_sec += delta_t;
                stats.power_joules += power_w * delta_t;
            }
        }

        if (current_fd_ >= 0) {
            close(current_fd_);
            current_fd_ = -1;
        }
        if (voltage_fd_ >= 0) {
            close(voltage_fd_);
            voltage_fd_ = -1;
        }
        if (status_fd_ >= 0) {
            close(status_fd_);
            status_fd_ = -1;
        }
    }

public:
    PowerMonitorTarget() {
        current_app_storage_ = "";
        current_app_ptr_.store(&current_app_storage_, std::memory_order_release);
    }
    
    ~PowerMonitorTarget() {
        stop();
    }

    std::string getName() const override {
        return "powerdata";
    }
    
    nlohmann::json read() const override {
        std::lock_guard<std::mutex> lock(data_mutex_);
        
        nlohmann::json result = nlohmann::json::array();
        
        for (const auto& [name, stats] : app_power_map_) {
            nlohmann::json app_data;
            app_data["name"] = name;
            app_data["power_joules"] = stats.power_joules;
            app_data["time_sec"] = stats.time_sec;
            result.push_back(std::move(app_data));
        }
        
        return result;
    }
    
    nlohmann::json write(const nlohmann::json& data) override {
        return {{"status", "error"}, {"message", "Power monitor target is read-only"}};
    }
    
    bool start() {
        if (running_.exchange(true)) {
            // 已在运行
            return false;
        }
        
        try {
            LOGD("Starting Monitor");
            worker_thread_ = std::thread(&PowerMonitorTarget::worker_loop, this);
            return true;
        } catch (const std::exception& e) {
            running_.store(false);
            return false;
        }
    }
    
    void stop() {
        if (!running_.exchange(false)) {
            return; // 已停止
        }
        
        // 通知工作线程退出
        cv_.notify_all();
        LOGD("Stoping Monitor");
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
    }
    
    void setForegroundApp(const std::string& app_name) {
        current_app_storage_ = app_name;
        current_app_ptr_.store(&current_app_storage_, std::memory_order_release);
    }
    
    void setScreenStatus(bool screen_on) {
        if (screen_on == screen_on_.load(std::memory_order_relaxed)) {
            return;
        }
        screen_on_.store(screen_on, std::memory_order_release);
    }

    void getCurrentState(std::string& app_name, bool& screen_status) const {
        std::string* app_ptr = current_app_ptr_.load(std::memory_order_acquire);
        app_name = app_ptr ? *app_ptr : "";
        screen_status = screen_on_.load(std::memory_order_acquire);
    }
    
    bool isRunning() const {
        return running_.load(std::memory_order_relaxed);
    }
    
    void clearStats() {
        std::lock_guard<std::mutex> lock(data_mutex_);
        app_power_map_.clear();
    }
};

#endif // POWERMONITORTARGET_HPP