/*****************************************
 * This module is designed to provide framework support for parsing JSON read/write commands.
 * Developed by DeepSeek under the supervision and guidance of BCK.
*****************************************/
#ifndef JSON_SOCKET_H
#define JSON_SOCKET_H

#include <UnixSocketServer.hpp>
#include <nlohmann/json.hpp>
#include <Alog.hpp>
#include <string>
#include <atomic>
#include <memory>
#include <unordered_map>

// 配置目标基类
class ConfigTarget {
public:
    virtual ~ConfigTarget() = default;
    
    // 获取目标名称
    virtual std::string getName() const = 0;
    
    // 读取配置
    virtual nlohmann::json read() const = 0;
    
    // 写入配置
    virtual nlohmann::json write(const nlohmann::json& data) = 0;
};

using ConfigTargetPtr = std::shared_ptr<ConfigTarget>;

class JSONSocket {
private:
    static std::unique_ptr<UnixSocketServer> server;
    static std::atomic<bool> initialized;
    static std::string socketPath;

    // 配置目标管理器
    static std::unordered_map<std::string, ConfigTargetPtr> configTargets;

public:
    // 初始化函数
    static bool initialize(const std::string& socket_path = "/tmp/JSONSocket");
    
    static void stop();
    static bool isRunning();
    
    // 注册配置目标
    static bool registerConfigTarget(const ConfigTargetPtr& target);
    
    // 注销配置目标
    static bool unregisterConfigTarget(const std::string& name);

private:
    // 请求处理
    static std::string handleRequest(const nlohmann::json& request);
};

#endif // JSON_SOCKET_H