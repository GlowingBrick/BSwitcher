#ifndef JSON_MODULE_H
#define JSON_MODULE_H

#include "JSONSocket.hpp"
#include <fstream>
#include <vector>
#include <string>
#include <mutex>
#include <sstream>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <array>
#include <thread>
#include <future>
#include <atomic>
#include <unordered_map>
#include <unordered_set>
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
        bool power_monitoring;
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
        {"mode_file", ""},
        {"screen_off", "powersave"},
        {"power_monitoring", true}
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
                config.mode_file = fileData.value("mode_file", DEFAULT_CONFIG["mode_file"]);
                config.screen_off = fileData.value("screen_off", DEFAULT_CONFIG["screen_off"]);
                config.power_monitoring = fileData.value("power_monitoring", DEFAULT_CONFIG["power_monitoring"]);
            } else {
                // 文件不存在或无效，使用默认值
                config.poll_interval = DEFAULT_CONFIG["poll_interval"];
                config.low_battery_threshold = DEFAULT_CONFIG["low_battery_threshold"];
                config.scene = DEFAULT_CONFIG["scene"];
                config.mode_file = DEFAULT_CONFIG["mode_file"];
                config.screen_off = DEFAULT_CONFIG["screen_off"];
                config.power_monitoring = DEFAULT_CONFIG["power_monitoring"];
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
            fileData["mode_file"] = config.mode_file;
            fileData["screen_off"] = config.screen_off;
            fileData["power_monitoring"] = config.power_monitoring;
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
        result["mode_file"] = config.mode_file;
        result["screen_off"] = config.screen_off;
        result["power_monitoring"] = config.power_monitoring;
        return result;
    }
    
    nlohmann::json write(const nlohmann::json& data) override {
        {
            std::lock_guard<std::mutex> lock(configMutex);
            if (data.contains("poll_interval")) {
                config.poll_interval = data["poll_interval"];
            }
            if (data.contains("low_battery_threshold")) {
                config.low_battery_threshold = data["low_battery_threshold"];
            }
            if (data.contains("scene")) {
                config.scene = data["scene"];
            }
            if (data.contains("mode_file")) {
                config.mode_file = data["mode_file"];
            }
            if (data.contains("screen_off")) {
                config.screen_off = data["screen_off"];
            }
            if (data.contains("power_monitoring")) {
                config.power_monitoring = data["power_monitoring"];
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
                config.defaultMode = data["defaultMode"];
            }
            
            config.apps.clear();
            if (data.contains("rules") && data["rules"].is_array()) {
                for (const auto& rule : data["rules"]) {
                    AppMode appMode;
                    if (rule.contains("appPackage")) {
                        appMode.pkgName = rule["appPackage"];
                    }
                    if (rule.contains("mode")) {
                        appMode.mode = rule["mode"];
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

// 信息模块
class InfoConfigTarget : public ConfigTarget {
public:
    // 内存结构体 - 公开访问
    struct InfoData {
        std::string name;
        std::string author;
        std::string version;
    } data;
    
    // 互斥锁 - 公开访问
    mutable std::mutex dataMutex;
    
public:
    InfoConfigTarget(const std::string& name = "Unknow", 
                    const std::string& author = "", 
                    const std::string& version = "") {
        std::lock_guard<std::mutex> lock(dataMutex);
        data.name = name;
        data.author = author;
        data.version = version;
    }
    
    std::string getName() const override {
        return "info";
    }
    
    nlohmann::json read() const override {
        std::lock_guard<std::mutex> lock(dataMutex);
        nlohmann::json result;
        result["name"] = data.name;
        result["author"] = data.author;
        result["version"] = data.version;
        return result;
    }
    
    nlohmann::json write(const nlohmann::json& jsonData) override {
        // info 是只读的，返回错误
        return {{"status", "error"}, {"message", "Info target is read-only"}};
    }
    
    void setData(const std::string& name, const std::string& author, const std::string& version) {
        std::lock_guard<std::mutex> lock(dataMutex);
        data.name = name;
        data.author = author;
        data.version = version;
    }
};

// 应用列表模块

class ApplistConfigTarget : public ConfigTarget {
public:
    ApplistConfigTarget() {
         buildFullCache();  //启动时建立应用列表缓存
    }
    std::string getName() const override {
        return "applist";
    }
    
    nlohmann::json read() const override {
        return getCachedAppList();
    }
    
    nlohmann::json write(const nlohmann::json& data) override {
        return {{"status", "error"}, {"message", "Applist target is read-only"}};
    }

private:
    // 缓存相关成员
    mutable std::mutex cacheMutex_;
    mutable nlohmann::json cachedAppList_ = nlohmann::json::array();
    mutable std::unordered_map<std::string, std::string> packageToAppName_; // 包名到应用名的映射
    mutable bool isCacheValid_ = false;
    
    // 获取带缓存的应用列表
    nlohmann::json getCachedAppList() const {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        
        if (!isCacheValid_) {
            buildFullCache();
        } else {
            // 后续访问，增量更新缓存
            updateCacheIncrementally();
        }
        
        return cachedAppList_;
    }
    
    // 构建完整缓存
    void buildFullCache() const {
        auto packages = executeCommand("pm list packages");
        std::vector<std::string> packageList = parsePackageList(packages);
        
        if (packageList.empty()) {
            cachedAppList_ = nlohmann::json::array();
            packageToAppName_.clear();
            isCacheValid_ = true;
            return;
        }
        
        // 并发处理所有包
        unsigned int numThreads = std::max(1u, std::thread::hardware_concurrency());
        numThreads = std::min(numThreads, static_cast<unsigned int>(packageList.size()));
        std::vector<std::vector<std::string>> chunks = splitVector(packageList, numThreads);
        
        std::vector<std::future<nlohmann::json>> futures;
        for (auto& chunk : chunks) {
            if (!chunk.empty()) {
                futures.push_back(std::async(std::launch::async, 
                    &ApplistConfigTarget::processPackageChunk, this, chunk));
            }
        }
        
        // 收集结果并构建缓存
        cachedAppList_ = nlohmann::json::array();
        packageToAppName_.clear();
        
        for (auto& future : futures) {
            nlohmann::json chunkResult = future.get();
            if (chunkResult.is_array()) {
                for (const auto& app : chunkResult) {
                    std::string packageName = app["package_name"];
                    std::string appName = app["app_name"];
                    
                    cachedAppList_.push_back(app);
                    packageToAppName_[packageName] = appName;
                }
            }
        }
        
        isCacheValid_ = true;
    }
    
    // 增量更新缓存
    void updateCacheIncrementally() const {
        auto packages = executeCommand("pm list packages");
        std::vector<std::string> currentPackages = parsePackageList(packages);
        
        // 构建当前包名的集合用于快速查找
        std::unordered_set<std::string> currentPackageSet(currentPackages.begin(), currentPackages.end());
        
        // 找出需要删除的包（在缓存中但不在当前列表中的包）
        std::vector<std::string> packagesToRemove;
        for (const auto& cachedPair : packageToAppName_) {
            if (currentPackageSet.find(cachedPair.first) == currentPackageSet.end()) {
                packagesToRemove.push_back(cachedPair.first);
            }
        }
        
        // 找出需要添加的包（在当前列表中但不在缓存中的包）
        std::vector<std::string> packagesToAdd;
        for (const auto& package : currentPackages) {
            if (packageToAppName_.find(package) == packageToAppName_.end()) {
                packagesToAdd.push_back(package);
            }
        }
        
        // 执行删除操作
        for (const auto& packageToRemove : packagesToRemove) {
            packageToAppName_.erase(packageToRemove);
        }
        
        // 执行添加操作（并发处理新增包）
        if (!packagesToAdd.empty()) {
            unsigned int numThreads = std::max(1u, std::thread::hardware_concurrency());
            numThreads = std::min(numThreads, static_cast<unsigned int>(packagesToAdd.size()));
            std::vector<std::vector<std::string>> chunks = splitVector(packagesToAdd, numThreads);
            
            std::vector<std::future<nlohmann::json>> futures;
            for (auto& chunk : chunks) {
                if (!chunk.empty()) {
                    futures.push_back(std::async(std::launch::async, 
                        &ApplistConfigTarget::processPackageChunk, this, chunk));
                }
            }
            
            // 收集新增包的结果
            for (auto& future : futures) {
                nlohmann::json chunkResult = future.get();
                if (chunkResult.is_array()) {
                    for (const auto& app : chunkResult) {
                        std::string packageName = app["package_name"];
                        std::string appName = app["app_name"];
                        packageToAppName_[packageName] = appName;
                    }
                }
            }
        }
        
        // 重新构建 JSON 数组
        rebuildJsonArray();
    }
    
    // 重新构建 JSON 数组
    void rebuildJsonArray() const {
        cachedAppList_ = nlohmann::json::array();
        for (const auto& pair : packageToAppName_) {
            cachedAppList_.push_back({
                {"app_name", pair.second},
                {"package_name", pair.first}
            });
        }
    }
    
    // 处理一批包
    nlohmann::json processPackageChunk(const std::vector<std::string>& packages) const {
        nlohmann::json chunkResult = nlohmann::json::array();
        
        for (const auto& package : packages) {
            nlohmann::json appInfo = processSinglePackage(package);
            if (!appInfo.is_null()) {
                chunkResult.push_back(appInfo);
            }
        }
        
        return chunkResult;
    }
    
    // 处理单个包
    nlohmann::json processSinglePackage(const std::string& package) const {
        std::string pmCmd = "pm path \"" + package + "\"";
        auto pathOutput = executeCommand(pmCmd.c_str());
        
        std::string apkPath = extractApkPath(pathOutput);
        std::string appName = package; // 默认使用包名
        
        if (!apkPath.empty() && access(apkPath.c_str(), F_OK) == 0) {
            std::string appLabel = getAppLabelFromApk(apkPath);
            if (!appLabel.empty()) {
                appName = appLabel;
            }
        }
        
        return nlohmann::json{
            {"app_name", appName},
            {"package_name", package}
        };
    }
    
    // 从 APK 获取应用标签
    std::string getAppLabelFromApk(const std::string& apkPath) const {
        std::string aaptCmd = "./aapt dump badging \"" + apkPath + "\" 2>/dev/null";
        auto aaptOutput = executeCommand(aaptCmd.c_str());
        
        std::istringstream aaptStream(aaptOutput);
        std::string line;
        
        while (std::getline(aaptStream, line)) {
            if (line.find("application-label:") != std::string::npos) {
                size_t start = line.find("application-label:") + 18;
                std::string label = line.substr(start);
                return cleanAppLabel(label);
            }
            
            if (line.find("application-label-zh") != std::string::npos) {
                size_t start = line.find(":") + 1;
                std::string label = line.substr(start);
                return cleanAppLabel(label);
            }
        }
        
        return "";
    }
    
    // 清理应用标签
    std::string cleanAppLabel(const std::string& label) const {
        std::string cleaned = label;
        
        cleaned.erase(std::remove(cleaned.begin(), cleaned.end(), '\''), cleaned.end());
        cleaned.erase(std::remove(cleaned.begin(), cleaned.end(), '\n'), cleaned.end());
        cleaned.erase(std::remove(cleaned.begin(), cleaned.end(), '\r'), cleaned.end());
        cleaned.erase(std::remove(cleaned.begin(), cleaned.end(), '\t'), cleaned.end());
        
        cleaned.erase(0, cleaned.find_first_not_of(" "));
        cleaned.erase(cleaned.find_last_not_of(" ") + 1);
        
        return cleaned;
    }
    
    // 解析包列表
    std::vector<std::string> parsePackageList(const std::string& packageOutput) const {
        std::vector<std::string> packageList;
        std::istringstream stream(packageOutput);
        std::string line;
        
        while (std::getline(stream, line)) {
            if (line.find("package:") == 0) {
                packageList.push_back(line.substr(8));
            }
        }
        
        return packageList;
    }
    
    // 提取 APK 路径
    std::string extractApkPath(const std::string& pathOutput) const {
        std::istringstream stream(pathOutput);
        std::string line;
        
        if (std::getline(stream, line)) {
            if (line.find("package:") == 0) {
                return line.substr(8);
            }
        }
        
        return "";
    }
    
    // 执行命令
    std::string executeCommand(const char* cmd) const {
        std::array<char, 128> buffer;
        std::string result;
        
        std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd, "r"), pclose);
        if (!pipe) {
            return "";
        }
        
        while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
            result += buffer.data();
        }
        
        return result;
    }
    
    // 分割向量
    std::vector<std::vector<std::string>> splitVector(const std::vector<std::string>& vec, unsigned int chunks) const {
        std::vector<std::vector<std::string>> result;
        size_t chunkSize = vec.size() / chunks;
        size_t remainder = vec.size() % chunks;
        
        size_t begin = 0;
        for (unsigned int i = 0; i < chunks; ++i) {
            size_t end = begin + chunkSize + (i < remainder ? 1 : 0);
            result.push_back(std::vector<std::string>(vec.begin() + begin, vec.begin() + end));
            begin = end;
        }
        
        return result;
    }
};

class ConfigListTarget : public ConfigTarget {
public:
    nlohmann::json data;
    ConfigListTarget(const nlohmann::json& list){
        data=list;
    }
    
    std::string getName() const override {
        return "configlist";
    }
    
    nlohmann::json read() const override {
        return data;
    }
    
    nlohmann::json write(const nlohmann::json& jsonData) override {
        // info 是只读的，返回错误
        return {{"status", "error"}, {"message", "Config list target is read-only"}};
    }
    
};

class AvailableModesTarget : public ConfigTarget {
public:
    nlohmann::json data;
    std::string getName() const override {
        return "availableModes";
    }
    
    nlohmann::json read() const override {
        return  {"powersave", "balance", "performance", "fast"};
    }
    
    nlohmann::json write(const nlohmann::json& jsonData) override {
        // 只读，返回错误
        return {{"status", "error"}, {"message", "Available Modes list target is read-only"}};
    }
    
};

#endif // JSON_MODULE_H