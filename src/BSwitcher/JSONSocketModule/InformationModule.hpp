/*维护用于前端显示的信息*/
#ifndef INFO_MODULE_HPP
#define INFO_MODULE_HPP

#include "JSONSocket/JSONSocket.hpp"
#include <mutex>
#include <string>

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

    nlohmann::json read() override {
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

class SimpleDataTarget : public ConfigTarget {  //简单的信息模块
private:
    nlohmann::json _data;
    std::string _name;

public:
    SimpleDataTarget(std::string name, const nlohmann::json& data) {
        _data = data;
        _name = name;
    }

    std::string getName() const override {
        return _name;
    }

    nlohmann::json read() override {
        return _data;
    }

    nlohmann::json write(const nlohmann::json& jsonData) override {
        // info 是只读的，返回错误
        return {{"status", "error"}, {"message", _name + "target is read-only"}};
    }
};


#endif