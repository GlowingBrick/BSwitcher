/*维护用于前端显示的信息*/
#ifndef INFO_MODULE_HPP
#define INFO_MODULE_HPP

#include "JSONSocket/JSONSocket.hpp"
#include <string>
#include <mutex>


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

#endif