/*维护各配置文件加载与读写*/
#ifndef CONFIG_MODULE_HPP
#define CONFIG_MODULE_HPP

#include "JSONSocket/JSONSocket.hpp"
#include <fstream>
#include <vector>
#include <string>
#include <mutex>

// 文件配置目标基类
class FileConfigTarget : public ConfigTarget {
protected:
    std::string filename;
    
public:
    explicit FileConfigTarget(const std::string& filename) : filename(filename) {}
    
    nlohmann::json write(const nlohmann::json& data) override {
        try {
            std::ofstream file(filename);
            if (!file.is_open()) {
                LOGE("Cannot open file for writing: %s", filename.c_str());
                return {{"status", "error"}, {"message", "Cannot open file for writing"}};
            }
            file << data.dump(4);
            return {{"status", "success"}};
        } catch (const std::exception& e) {
            LOGE("Failed to write file %s: %s", filename.c_str(), e.what());
            return {{"status", "error"}, {"message", "Failed to write file"}};
        }
    }
    
    nlohmann::json read() const override {
        try {
            std::ifstream file(filename);
            if (!file.is_open()) {
                LOGW("Cannot open file for reading: %s", filename.c_str());
                return {{"status", "error"}, {"message", "Cannot open file for reading"}};
            }
            nlohmann::json data;
            file >> data;
            return data;
        } catch (const std::exception& e) {
            LOGE("Failed to read file %s: %s", filename.c_str(), e.what());
            return {{"status", "error"}, {"message", "Failed to read file"}};
        }
    }
};

// 主配置模块
class MainConfigTarget : public FileConfigTarget {
public:
    // 内存结构体 - 公开访问
    struct MainConfig {
        int poll_interval;
        int low_battery_threshold;
        bool scene;
        std::string mode_file;
        std::string screen_off;
        bool scene_strict;
        bool power_monitoring;
        bool using_inotify;
    } config;
    
    // 互斥锁 - 公开访问
    mutable std::mutex configMutex;
    bool modify;    //标记是否修改
private:
    // 默认配置
    const nlohmann::json DEFAULT_CONFIG = {
        {"poll_interval", 2},
        {"low_battery_threshold", 15},
        {"scene", true},
        {"scene_strict", false},
        {"mode_file", ""},
        {"screen_off", "powersave"},
        {"power_monitoring", true},
        {"using_inotify", true}
    };
    
    void loadFromFile() {
        auto fileData = FileConfigTarget::read();
        bool hasValidData = false;
        
        // 检查文件是否包含有效数据
        if (!fileData.is_null() && fileData.is_object()) {
            hasValidData = true;
        }
        
        {
            std::lock_guard<std::mutex> lock(configMutex);
            
            if (hasValidData) {
                // 逐项加载，缺失的项使用默认值
                config.poll_interval = fileData.value("poll_interval", DEFAULT_CONFIG["poll_interval"]);
                config.low_battery_threshold = fileData.value("low_battery_threshold", DEFAULT_CONFIG["low_battery_threshold"]);
                config.scene = fileData.value("scene", DEFAULT_CONFIG["scene"]);
                config.scene_strict = fileData.value("scene_strict", DEFAULT_CONFIG["scene_strict"]);
                config.mode_file = fileData.value("mode_file", DEFAULT_CONFIG["mode_file"]);
                config.screen_off = fileData.value("screen_off", DEFAULT_CONFIG["screen_off"]);
                config.power_monitoring = fileData.value("power_monitoring", DEFAULT_CONFIG["power_monitoring"]);
                config.using_inotify = fileData.value("using_inotify", DEFAULT_CONFIG["using_inotify"]);
            } else {
                // 文件不存在或无效，使用默认值
                config.poll_interval = DEFAULT_CONFIG["poll_interval"];
                config.low_battery_threshold = DEFAULT_CONFIG["low_battery_threshold"];
                config.scene = DEFAULT_CONFIG["scene"];
                config.scene_strict = DEFAULT_CONFIG["scene_strict"];
                config.mode_file = DEFAULT_CONFIG["mode_file"];
                config.screen_off = DEFAULT_CONFIG["screen_off"];
                config.power_monitoring = DEFAULT_CONFIG["power_monitoring"];
                config.using_inotify = DEFAULT_CONFIG["config.using_inotify ="];
            }
            modify=true;
        }
        
        // 如果文件不存在或数据不完整，不立即写入，等待前端修改时再写入
        if (!hasValidData) {
            LOGW("Config file %s is missing or invalid, using default values", filename.c_str());
        }
    }
    
    nlohmann::json writeToFile() {
        nlohmann::json fileData;
        {
            std::lock_guard<std::mutex> lock(configMutex);
            fileData["poll_interval"] = config.poll_interval;
            fileData["low_battery_threshold"] = config.low_battery_threshold;
            fileData["scene"] = config.scene;
            fileData["scene_strict"] = config.scene_strict;
            fileData["mode_file"] = config.mode_file;
            fileData["screen_off"] = config.screen_off;
            fileData["power_monitoring"] = config.power_monitoring;
            fileData["using_inotify"] = config.using_inotify;
        }
        return FileConfigTarget::write(fileData);
    }
    
public:
    MainConfigTarget() : FileConfigTarget("config.json") {
        loadFromFile();
    }
    
    std::string getName() const override {
        return "config";
    }
    
    nlohmann::json read() const override {
        std::lock_guard<std::mutex> lock(configMutex);
        nlohmann::json result;
        result["poll_interval"] = config.poll_interval;
        result["low_battery_threshold"] = config.low_battery_threshold;
        result["scene"] = config.scene;
        result["scene_strict"] = config.scene_strict;
        result["mode_file"] = config.mode_file;
        result["screen_off"] = config.screen_off;
        result["power_monitoring"] = config.power_monitoring;
        result["using_inotify"] = config.using_inotify;
        return result;
    }
    
    nlohmann::json write(const nlohmann::json& data) override {
        {
            std::lock_guard<std::mutex> lock(configMutex);
            if (data.contains("poll_interval")) {
                config.poll_interval = data.value("poll_interval", config.poll_interval);
            }
            if (data.contains("low_battery_threshold")) {
                config.low_battery_threshold = data.value("low_battery_threshold", config.low_battery_threshold);
            }
            if (data.contains("scene")) {
                config.scene = data.value("scene", config.scene);
            }
            if (data.contains("scene_strict")) {
                config.scene_strict = data.value("scene_strict", config.scene_strict);
            }
            if (data.contains("mode_file")) {
                config.mode_file = data.value("mode_file", config.mode_file);
            }
            if (data.contains("screen_off")) {
                config.screen_off = data.value("screen_off", config.screen_off);
            }
            if (data.contains("power_monitoring")) {
                config.power_monitoring = data.value("power_monitoring", config.power_monitoring);
            }
            if (data.contains("using_inotify")) {
                config.using_inotify = data.value("using_inotify", config.using_inotify);
            }
        }
        modify=true;
        
        return writeToFile();
    }
};

// 调度器配置模块
class SchedulerConfigTarget : public FileConfigTarget {
public:
    // 内存结构体 - 公开访问
    struct AppMode {
        std::string pkgName;
        std::string mode;
    };
    
    struct SchedulerConfig {
        std::string defaultMode;
        std::vector<AppMode> apps;
    } config;
    
    // 互斥锁 - 公开访问
    mutable std::mutex configMutex;
    
private:
    // 默认配置
    const nlohmann::json DEFAULT_CONFIG = {
        {"defaultMode", "performance"},
        {"rules", nlohmann::json::array()}
    };
    
    void loadFromFile() {
        auto fileData = FileConfigTarget::read();
        bool hasValidData = false;
        
        // 检查文件是否包含有效数据
        if (!fileData.is_null() && fileData.is_object()) {
            hasValidData = true;
        }
        
        {
            std::lock_guard<std::mutex> lock(configMutex);
            
            if (hasValidData) {
                // 逐项加载，缺失的项使用默认值
                config.defaultMode = fileData.value("defaultMode", DEFAULT_CONFIG["defaultMode"]);
                
                config.apps.clear();
                if (fileData.contains("rules") && fileData["rules"].is_array()) {
                    for (const auto& rule : fileData["rules"]) {
                        AppMode appMode;
                        appMode.pkgName = rule.value("appPackage", "");
                        appMode.mode = rule.value("mode", "");
                        if (!appMode.pkgName.empty() && !appMode.mode.empty()) {
                            config.apps.push_back(appMode);
                        }
                    }
                }
            } else {
                // 文件不存在或无效，使用默认值
                config.defaultMode = DEFAULT_CONFIG["defaultMode"];
                config.apps.clear();
            }
        }
        
        // 如果文件不存在或数据不完整，不立即写入，等待前端修改时再写入
        if (!hasValidData) {
            LOGW("Scheduler config file %s is missing or invalid, using default values", filename.c_str());
        }
    }
    
    nlohmann::json writeToFile() {
        nlohmann::json fileData;
        {
            std::lock_guard<std::mutex> lock(configMutex);
            fileData["defaultMode"] = config.defaultMode;
            
            nlohmann::json rulesArray = nlohmann::json::array();
            for (const auto& app : config.apps) {
                nlohmann::json rule;
                rule["appPackage"] = app.pkgName;
                rule["mode"] = app.mode;
                rulesArray.push_back(rule);
            }
            fileData["rules"] = rulesArray;
        }
        return FileConfigTarget::write(fileData);
    }
    
public:
    SchedulerConfigTarget() : FileConfigTarget("scheduler_config.json") {
        loadFromFile();
    }
    
    std::string getName() const override {
        return "scheduler";
    }
    
    nlohmann::json read() const override {
        std::lock_guard<std::mutex> lock(configMutex);
        nlohmann::json result;
        result["defaultMode"] = config.defaultMode;
        
        nlohmann::json rulesArray = nlohmann::json::array();
        for (const auto& app : config.apps) {
            nlohmann::json rule;
            rule["appPackage"] = app.pkgName;
            rule["mode"] = app.mode;
            rulesArray.push_back(rule);
        }
        result["rules"] = rulesArray;
        
        return result;
    }
    
    nlohmann::json write(const nlohmann::json& data) override {
        {
            std::lock_guard<std::mutex> lock(configMutex);
            if (data.contains("defaultMode")) {
                config.defaultMode = data.value("defaultMode",config.defaultMode);
            }
            
            config.apps.clear();
            if (data.contains("rules") && data["rules"].is_array()) {
                for (const auto& rule : data["rules"]) {
                    AppMode appMode;
                    if (rule.contains("appPackage")) {
                        appMode.pkgName = rule.value("appPackage","");
                    }
                    if (rule.contains("mode")) {
                        appMode.mode = rule.value("mode","");
                    }
                    if (!appMode.pkgName.empty() && !appMode.mode.empty()) {
                        config.apps.push_back(appMode);
                    }
                }
            }
        }
        
        return writeToFile();
    }
};

#endif