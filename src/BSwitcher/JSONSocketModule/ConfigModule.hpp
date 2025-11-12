/*维护各配置文件加载与读写*/
#ifndef CONFIG_MODULE_HPP
#define CONFIG_MODULE_HPP

#include "JSONSocket/JSONSocket.hpp"
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

//这是真正配置文件里面有的
#define CONFIG_ITEMS                                  \ 
    CONFIG_ITEM(int, poll_interval, 2)                \
    CONFIG_ITEM(int, low_battery_threshold, 15)       \
    CONFIG_ITEM(bool, scene, true)                    \
    CONFIG_ITEM(std::string, mode_file, "")           \
    CONFIG_ITEM(std::string, screen_off, "powersave") \
    CONFIG_ITEM(bool, scene_strict, false)            \
    CONFIG_ITEM(bool, power_monitoring, true)         \
    CONFIG_ITEM(bool, using_inotify, true)            \
    CONFIG_ITEM(bool, dual_battery, false)            \
    CONFIG_ITEM(std::string, custom_mode, "")


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

    nlohmann::json read() override {
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
        #define CONFIG_ITEM(type, name, default_val) type name;
                CONFIG_ITEMS
        #undef CONFIG_ITEM
    } config;

    // 互斥锁 - 公开访问
    mutable std::mutex configMutex;
    bool modify;  //标记是否修改
private:
    // 默认配置
    const nlohmann::json DEFAULT_CONFIG = {
        #define CONFIG_ITEM(type, name, default_val) {#name, default_val},
                CONFIG_ITEMS
        #undef CONFIG_ITEM
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
            
            #define CONFIG_ITEM(type, name, default_val) \
                    config.name = fileData.value(#name, DEFAULT_CONFIG[#name]);
                    CONFIG_ITEMS
            #undef CONFIG_ITEM

        } else {

            #define CONFIG_ITEM(type, name, default_val) \
                    config.name = DEFAULT_CONFIG[#name];
                    CONFIG_ITEMS
            #undef CONFIG_ITEM
        }
            modify = true;
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
            #define CONFIG_ITEM(type, name, default_val) \
                fileData[#name] = config.name;
                CONFIG_ITEMS
            #undef CONFIG_ITEM
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

    nlohmann::json read() override {
        std::lock_guard<std::mutex> lock(configMutex);
        nlohmann::json result;

        #define CONFIG_ITEM(type, name, default_val) result[#name] = config.name;
        CONFIG_ITEMS
        #undef CONFIG_ITEM

        return result;
    }

    nlohmann::json write(const nlohmann::json& data) override {
        {
            std::lock_guard<std::mutex> lock(configMutex);

            #define CONFIG_ITEM(type, name, default_val) \
                if (data.contains(#name)) { \
                    config.name = data.value(#name, config.name); \
                }
                CONFIG_ITEMS
            #undef CONFIG_ITEM

        }
        modify = true;

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
        {"defaultMode", "balance"},
        {"rules", nlohmann::json::array()}};

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

    nlohmann::json read() override {
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
                config.defaultMode = data.value("defaultMode", config.defaultMode);
            }

            config.apps.clear();
            if (data.contains("rules") && data["rules"].is_array()) {
                for (const auto& rule : data["rules"]) {
                    AppMode appMode;
                    if (rule.contains("appPackage")) {
                        appMode.pkgName = rule.value("appPackage", "");
                    }
                    if (rule.contains("mode")) {
                        appMode.mode = rule.value("mode", "");
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

class ConfigButtonTarget : public ConfigTarget {  //按钮事件
private:
    std::function<std::string(const std::string&)> callback_;

public:
    ConfigButtonTarget(std::function<std::string(const std::string&)> callback)
        : callback_(callback) {}

    std::string getName() const override {
        return "command";
    }

    nlohmann::json read() override {
        return {{"status", "error"}, {"message", "command is write-only, no data to read."}};
    }

    nlohmann::json write(const nlohmann::json& jsonData) override {
        if (!jsonData.is_array()) {  //检查数据
            return {{"status", "error"}, {"message", "Unparseable Command."}};
        }

        if (jsonData.empty() || !jsonData[0].is_string()) {
            return {{"status", "error"}, {"message", "Unparseable Command."}};
        }

        if (callback_) {
            std::string result = callback_(jsonData[0]);
            return {{"message", result}};
        } else {
            return {{"status", "error"}, {"message", "Backend not properly initialized."}};
        }
    }
};

#endif