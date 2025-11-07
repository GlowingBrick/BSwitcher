#ifndef JSON_SOCKET_HPP
#define JSON_SOCKET_HPP

#include <UnixSocketServer/UnixSocketServer.hpp>
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
    std::unique_ptr<UnixSocketServer> server;
    std::atomic<bool> initialized;
    std::string socketPath;

    // 实例管理器
    std::unordered_map<std::string, ConfigTargetPtr> configTargets;

public:
    JSONSocket(const std::string& socket_path = "/tmp/JSONSocket");
    ~JSONSocket();
    
    // 禁止拷贝和赋值
    JSONSocket(const JSONSocket&) = delete;
    JSONSocket& operator=(const JSONSocket&) = delete;
    JSONSocket(JSONSocket&&) = delete;
    JSONSocket& operator=(JSONSocket&&) = delete;

    // 初始化函数
    bool initialize();
    void stop();
    bool isRunning() const;
    
    // 注册配置目标
    bool registerConfigTarget(const ConfigTargetPtr& target);
    
    // 注销配置目标
    bool unregisterConfigTarget(const std::string& name);
    
    // 获取已注册的目标数量
    size_t getTargetCount() const { return configTargets.size(); }
    
    // 获取socket路径
    const std::string& getSocketPath() const { return socketPath; }

private:
    // 请求处理
    std::string handleRequest(const nlohmann::json& request);
};

#endif // JSON_SOCKET_HPP