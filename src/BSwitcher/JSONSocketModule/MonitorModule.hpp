/*用于监视系统电源状态*/
/*顺便检查屏幕*/
#ifndef POWER_MONITOR_MODULE_HPP
#define POWER_MONITOR_MODULE_HPP

#include "JSONSocket/JSONSocket.hpp"
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>
#include <unistd.h>
#include <unordered_map>

// 应用功耗记录结构体
struct AppPower {
    float time_sec = 0.0f;      // 应用运行时间（秒）
    float power_joules = 0.0f;  // 焦耳累计
};

class PowerMonitorTarget : public ConfigTarget {  //主管功耗监控
private:
    // 功耗数据存储
    std::unordered_map<std::string, AppPower> app_power_map_;
    mutable std::mutex data_mutex_;

    // 传感器文件描述符
    int current_fd_ = -1;
    int voltage_fd_ = -1;
    int status_fd_ = -1;

    bool* dualBatteryPtr_;
    std::atomic<int> unit_{12};

    // 线程控制
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_{false};
    std::thread worker_thread_;
    std::mutex control_mutex_;
    std::condition_variable cv_;

    const std::string* current_app_ptr_;

    std::atomic<bool> screen_status{true};

    // 初始化传感器
    bool init_power_sensors() {
        current_fd_ = open("/sys/class/power_supply/battery/current_now", O_RDONLY | O_CLOEXEC);
        voltage_fd_ = open("/sys/class/power_supply/battery/voltage_now", O_RDONLY | O_CLOEXEC);
        status_fd_ = open("/sys/class/power_supply/battery/status", O_RDONLY | O_CLOEXEC);
        return (current_fd_ >= 0) && (voltage_fd_ >= 0);  //status一般不是必须的
    }

    // 读取当前功率（瓦特）
    float read_current_power_w() {
        char buf[32] = {0};

        if (pread(current_fd_, buf, sizeof(buf) - 1, 0) <= 0) return 0.0f;
        long current_ua = atol(buf);
        if (current_ua <= 0) {
            return 0.0f;
        }

        memset(buf, 0, sizeof(buf));
        if (pread(voltage_fd_, buf, sizeof(buf) - 1, 0) <= 0) return 0.0f;
        long voltage_uv = atol(buf);

        // P = (I * V) / 1e12
        return static_cast<float>(static_cast<double>(current_ua) *
                                  static_cast<double>(voltage_uv) *
                                  std::pow(10.0, -unit_.load(std::memory_order_relaxed))) *  //转换到W
               ((*dualBatteryPtr_) ? 2.0 : 1.0);                                             //双电芯
    }

    // 工作线程
    void worker_loop() {
        if (!init_power_sensors()) {
            LOGE("Unable To Init Power Monitor");
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
            running_.store(false, std::memory_order_relaxed);
            return;
        }

        char battery_status = 0;
        float power_w;
        float delta_t;

        timespec current_time;
        timespec last_time;
        clock_gettime(CLOCK_MONOTONIC, &last_time);

        while (running_.load(std::memory_order_relaxed)) {

            {
                std::unique_lock<std::mutex> lock(control_mutex_);

                if (stop_.load(std::memory_order_relaxed)) {  //如果在stop中
                    LOGD("Screen Off,Power Monitor Stoped");
                    cv_.wait(lock, [this]() {  //等待
                                               //stop与run任意置false都退出阻塞
                        return ((!running_.load(std::memory_order_relaxed)) || (!stop_.load(std::memory_order_relaxed)));
                    });
                    LOGD("Screen On,Power Monitor Continue");
                    clock_gettime(CLOCK_MONOTONIC, &last_time);
                } else {
                    cv_.wait_for(lock, std::chrono::seconds(1), [this]() {
                        return ((!running_.load(std::memory_order_relaxed)));  //只考虑run
                    });
                }

                if (!running_.load(std::memory_order_relaxed)) {
                    break;
                }
            }

            if (!screen_status.load(std::memory_order_relaxed)) {  //熄屏时
                stop_.store(true, std::memory_order_relaxed);      //准备自我阻塞
                continue;
            }

            std::string app_name = *current_app_ptr_;

            if (app_name.empty()) {
                clock_gettime(CLOCK_MONOTONIC, &last_time);  //重置时间
                continue;
            }

            pread(status_fd_, &battery_status, 1, 0);

            if (battery_status == 'C' || battery_status == 'F') {  //充电时不统计
                clock_gettime(CLOCK_MONOTONIC, &last_time);
                continue;
            }

            power_w = read_current_power_w();

            if (power_w <= 1e-12f) {
                LOGD("Anomalous value detected, skipping.");
                clock_gettime(CLOCK_MONOTONIC, &last_time);
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
                stats.power_joules += (power_w * delta_t);
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

    void data_correction(int cycles = 0) {  //数据矫正，为解决不同设备单位问题
        if (cycles >= 5) {
            LOGE("PowerMonitor: We cannot calibrate this data. Manual calibration is required.");
            return;
        }
        if (app_power_map_.size() <= 1) {  //只有1条时暂时忽略
            return;
        }

        int tooLarge = 0;    //过大的数据量
        int tooSmall = 0;    //过小的数据量
        int normalData = 0;  //正常数据
        for (const auto& [name, stats] : app_power_map_) {
            if (stats.time_sec < 0.01f) {
                continue;
            }
            float watt = stats.power_joules / stats.time_sec;
            if (watt > 40.0) {  //数据过大，绝不应超过40
                ++tooLarge;
            } else if (watt < 0.041) {  //数据过小，不应低于0.04
                ++tooSmall;
            } else {
                ++normalData;
            }
        }

        if (normalData > (tooSmall + tooLarge)) {  //正常占多数
            return;
        } else {
            if (tooSmall > tooLarge) {
                for (auto& [name, stats] : app_power_map_) {  //纠正所有数据
                    stats.power_joules *= 1000.0;
                }
                int untmp = unit_.load(std::memory_order_relaxed);
                if (untmp - 3 < 0) {
                    return;
                    LOGE("PowerMonitor: We cannot calibrate this data. Manual calibration is required.");
                }
                unit_.store(untmp - 3, std::memory_order_relaxed);  //数量级扩大三倍
                LOGD("PowerMonitor: Data values too small, amplifying data.");
                data_correction(cycles + 1);  //递归继续检查

            } else if (tooSmall < tooLarge) {
                for (auto& [name, stats] : app_power_map_) {  //纠正所有数据
                    stats.power_joules /= 1000.0;
                }
                int untmp = unit_.load(std::memory_order_relaxed);
                unit_.store(untmp + 3, std::memory_order_relaxed);  //数量级缩小三倍
                LOGD("PowerMonitor: Data values too large, reducing data.");
                data_correction(cycles + 1);  //递归继续检查

            } else {
                //很难想象什么设备能同时出现大量的40w以上和0.4瓦以下
                LOGE("PowerMonitor: We cannot calibrate this data. Manual calibration is required.");
                return;
            }
        }
    }

    void trim_and_merge_app_power() {  //数据剪裁
        if (app_power_map_.size() <= 30) {
            return;
        }

        AppPower other_stats{0.0f, 0.0f};
        std::vector<std::pair<std::string, AppPower>> normal_apps;

        for (const auto& [name, stats] : app_power_map_) {
            if (name == "_other_") {
                other_stats.time_sec += stats.time_sec;
                other_stats.power_joules += stats.power_joules;
            } else {
                normal_apps.emplace_back(name, stats);
            }
        }

        if (normal_apps.size() <= 20) {
            app_power_map_["_other_"] = other_stats;
            return;
        }

        std::sort(normal_apps.begin(), normal_apps.end(),  //按功耗排序
                  [](const auto& a, const auto& b) {
                      return a.second.power_joules > b.second.power_joules;
                  });

        app_power_map_.clear();

        for (size_t i = 0; i < 20; i++) {  //保留前20
            app_power_map_[normal_apps[i].first] = normal_apps[i].second;
        }

        for (size_t i = 20; i < normal_apps.size(); i++) {  //在_other_中存储其余数据
            other_stats.time_sec += normal_apps[i].second.time_sec;
            other_stats.power_joules += normal_apps[i].second.power_joules;
        }

        app_power_map_["_other_"] = other_stats;
    }

public:
    PowerMonitorTarget(std::string* current_app_ptr, bool* dualBatteryRef)
        : dualBatteryPtr_(dualBatteryRef), current_app_ptr_(current_app_ptr) {}

    ~PowerMonitorTarget() {
        stop();
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

    std::string getName() const override {
        return "powerdata";
    }

    nlohmann::json read() override {
        std::lock_guard<std::mutex> lock(data_mutex_);

        trim_and_merge_app_power();  //发送前修复数据
        data_correction();

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
        stop_.store(false);
        if (running_.exchange(true)) {
            // 已在运行
            return false;
        }

        try {
            LOGI("Starting Monitor");
            worker_thread_ = std::thread(&PowerMonitorTarget::worker_loop, this);
            return true;
        } catch (const std::exception& e) {
            running_.store(false);
            return false;
        }
    }

    void stop() {
        if (!running_.exchange(false)) {
            return;  // 已停止
        }

        // 通知工作线程退出
        cv_.notify_all();
        LOGI("Stoping Monitor");
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
    }

    void setScreenStatus(bool sstatus) {                           //传入屏幕状态
        if (!running_.load(std::memory_order_relaxed) == false) {  //如果没有启用监视
            return;
        }

        screen_status.store(sstatus, std::memory_order_relaxed);
        if (stop_.load(std::memory_order_relaxed)) {  //如果监视线程休眠
            if (sstatus) {                            //如果亮屏
                stop_.store(false);
                cv_.notify_all();  //唤醒
            }
        }
    }

    bool isRunning() const {
        return running_.load(std::memory_order_relaxed);
    }

    void clearStats() {
        std::lock_guard<std::mutex> lock(data_mutex_);
        app_power_map_.clear();
        LOGI("Power consumption records cleaned up");
    }
};

#endif  // POWERMONITORTARGET_HPP