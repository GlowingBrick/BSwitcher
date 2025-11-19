/*维护各配置文件加载与读写*/
#ifndef CONFIG_MODULE_HPP
#define CONFIG_MODULE_HPP

#include "JSONSocket/JSONSocket.hpp"
#include <fstream>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <vector>

//这是真正配置文件里面有的
#define CONFIG_ITEMS                                  \
    CONFIG_ITEM(int, poll_interval, 2)                \
    CONFIG_ITEM(int, low_battery_threshold, 15)       \
    CONFIG_ITEM(bool, scene, true)                    \
    CONFIG_ITEM(bool, enable_dynamic, true)           \
    CONFIG_ITEM(std::string, mode_file, "")           \
    CONFIG_ITEM(std::string, screen_off, "powersave") \
    CONFIG_ITEM(bool, scene_strict, false)            \
    CONFIG_ITEM(bool, power_monitoring, true)         \
    CONFIG_ITEM(bool, using_inotify, true)            \
    CONFIG_ITEM(bool, dual_battery, false)            \
    CONFIG_ITEM(std::string, custom_mode, "")         \
    CONFIG_ITEM(bool, dynamic_fps, false)             \
    CONFIG_ITEM(int, fps_idle_time, 2500)             \
    CONFIG_ITEM(int, down_fps, 60)                    \
    CONFIG_ITEM(int, up_fps, 120)                     \
    CONFIG_ITEM(bool, fps_backdoor, false)            \
    CONFIG_ITEM(int, fps_backdoor_id, 1035)           \
    CONFIG_ITEM(std::string, screen_resolution, "")

// 文件配置目标基类
class FileConfigTarget : public ConfigTarget {
protected:
    std::string filename;
    time_t last_stat_time_ = 0;

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
        struct stat file_stat;
        stat(filename.c_str(), &file_stat);
        if (last_stat_time_ == file_stat.st_mtime) {
            return;
        }
        last_stat_time_ = file_stat.st_mtime;

        LOGD("Loading : %s", filename.c_str());

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

    template <typename T>
    T safe_json_get(const nlohmann::json& data, const std::string& key, const T& default_val) {  //为了解决前端有时候用字符表示数字的情况
        if (!data.contains(key)) {
            return default_val;
        }

        try {
            if (data[key].type() == nlohmann::json::value_t::number_integer &&
                std::is_integral_v<T>) {
                return data[key].get<T>();
            } else if (data[key].type() == nlohmann::json::value_t::number_float &&
                       std::is_floating_point_v<T>) {
                return data[key].get<T>();
            } else if (data[key].type() == nlohmann::json::value_t::string) {  //字符转数字
                if constexpr (std::is_integral_v<T>) {
                    if constexpr (std::is_same_v<T, bool>) {
                        std::string str = data[key].get<std::string>();
                        std::transform(str.begin(), str.end(), str.begin(), ::tolower);
                        return (str == "true" || str == "1" || str == "yes");
                    } else {
                        return static_cast<T>(std::stoll(data[key].get<std::string>()));
                    }
                } else if constexpr (std::is_floating_point_v<T>) {
                    return static_cast<T>(std::stod(data[key].get<std::string>()));
                }
            }
            return data[key].get<T>();
        } catch (...) {
            return default_val;
        }
    }

public:
    MainConfigTarget() : FileConfigTarget("config.json") {
        loadFromFile();
    }

    std::string getName() const override {
        return "config";
    }

    nlohmann::json read() override {
        loadFromFile();

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

#define CONFIG_ITEM(type, name, default_val)                         \
    if (data.contains(#name)) {                                      \
        config.name = safe_json_get<type>(data, #name, config.name); \
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
        int up_fps;
        int down_fps;
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
        struct stat file_stat;
        stat(filename.c_str(), &file_stat);
        if (last_stat_time_ == file_stat.st_mtime) {
            return;
        }
        last_stat_time_ = file_stat.st_mtime;

        LOGD("Loading : %s", filename.c_str());
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
                        appMode.up_fps = rule.value("up_fps", -1);
                        appMode.down_fps = rule.value("down_fps", -1);
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
                rule["up_fps"] = app.up_fps;
                rule["down_fps"] = app.down_fps;
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
        loadFromFile();

        std::lock_guard<std::mutex> lock(configMutex);
        nlohmann::json result;
        result["defaultMode"] = config.defaultMode;

        nlohmann::json rulesArray = nlohmann::json::array();
        for (const auto& app : config.apps) {
            nlohmann::json rule;
            rule["appPackage"] = app.pkgName;
            rule["mode"] = app.mode;
            rule["up_fps"] = app.up_fps;
            rule["down_fps"] = app.down_fps;
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

            std::vector<SchedulerConfigTarget::AppMode> tmpapplist;
            if (data.contains("rules") && data["rules"].is_array()) {
                for (const auto& rule : data["rules"]) {

                    if (rule.contains("appPackage")) {
                        if (rule["appPackage"].is_string()) {
                            AppMode appMode;

                            appMode.pkgName = rule.value("appPackage", "");
                            appMode.mode = rule.value("mode", config.defaultMode);
                            appMode.up_fps = rule.value("up_fps", -1);
                            appMode.down_fps = rule.value("down_fps", -1);

                            tmpapplist.push_back(appMode);
                        }
                    }
                }
                config.apps = tmpapplist;
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