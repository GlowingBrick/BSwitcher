#include "JSONSocket.hpp"
#include <vector>
#include <string>
#include <mutex>
#include <cstdlib>
#include <memory>
#include <array>
#include <thread>
#include <future>
#include <unordered_map>
#include <unordered_set>
#include <unistd.h> 

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
        std::string enpackname="";
        while (std::getline(aaptStream, line)) {
            if (line.find("application-label-zh:") != std::string::npos) {
                size_t start = line.find("application-label-zh:") + 21;
                std::string label = line.substr(start);
                return cleanAppLabel(label);
            }

            if (line.find("application-label:") != std::string::npos) {
                size_t start = line.find("application-label:") + 18;
                enpackname = line.substr(start);
            }
        }
        
        return cleanAppLabel(enpackname);
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
