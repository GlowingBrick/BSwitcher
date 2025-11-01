/*****************************************
 * This module is designed to provide framework support for parsing JSON read/write commands.
 * Developed by DeepSeek under the supervision and guidance of BCK.
*****************************************/
#include "JSONSocket.hpp"

// 静态成员定义
std::unique_ptr<UnixSocketServer> JSONSocket::server = nullptr;
std::atomic<bool> JSONSocket::initialized = false;
std::string JSONSocket::socketPath = "/tmp/JSONSocket";

// 配置目标管理器
std::unordered_map<std::string, ConfigTargetPtr> JSONSocket::configTargets;

bool JSONSocket::initialize(const std::string& socket_path) {
    if (initialized) {
        LOGW("JSONSocket already initialized");
        return true;
    }

    LOGI("JSONSocket initializing...");

    // 设置socket路径
    socketPath = socket_path;

    // 创建socket服务器
    auto callback = [](const nlohmann::json& request) -> std::string {
        return handleRequest(request);
    };

    server = std::make_unique<UnixSocketServer>(socketPath, callback, std::chrono::seconds(30));

    // 启动服务器
    if (server->start()) {
        initialized = true;
        LOGI("JSONSocket initialized successfully");
        LOGI("Socket path: %s", socketPath.c_str());
        LOGI("Registered config targets: %zu", configTargets.size());
        return true;
    } else {
        LOGE("Failed to start UnixSocketServer");
        server.reset();
        return false;
    }
}

void JSONSocket::stop() {
    if (server) {
        server->stop();
        server.reset();
    }
    initialized = false;
    configTargets.clear();
    LOGI("JSONSocket stopped");
}

bool JSONSocket::isRunning() {
    return initialized && server && server->isRunning();
}

bool JSONSocket::registerConfigTarget(const ConfigTargetPtr& target) {
    if (!target) {
        LOGE("Cannot register null config target");
        return false;
    }
    
    std::string name = target->getName();
    if (configTargets.find(name) != configTargets.end()) {
        LOGW("Config target %s already registered, replacing", name.c_str());
    }
    
    configTargets[name] = target;
    LOGI("Registered config target: %s", name.c_str());
    return true;
}

bool JSONSocket::unregisterConfigTarget(const std::string& name) {
    auto it = configTargets.find(name);
    if (it != configTargets.end()) {
        configTargets.erase(it);
        LOGI("Unregistered config target: %s", name.c_str());
        return true;
    }
    LOGW("Config target %s not found for unregister", name.c_str());
    return false;
}

std::string JSONSocket::handleRequest(const nlohmann::json& request) {
    try {
        if (!request.contains("target") || !request.contains("mode")) {
            nlohmann::json error_response;
            error_response["status"] = "error";
            error_response["message"] = "Missing required fields: target and mode";
            return error_response.dump();
        }

        std::string targetName = request["target"];
        std::string mode = request["mode"];

        LOGD("Processing request: target=%s, mode=%s", targetName.c_str(), mode.c_str());

        // 查找对应的配置目标
        auto it = configTargets.find(targetName);
        if (it == configTargets.end()) {
            nlohmann::json error_response;
            error_response["status"] = "error";
            error_response["message"] = "Invalid target: " + targetName;
            return error_response.dump();
        }

        auto target = it->second;

        if (mode == "read") {
            nlohmann::json result = target->read();
            LOGD("%s read response: %s", targetName.c_str(), result.dump().c_str());
            return result.dump();
            
        } else if (mode == "write") {
            if (!request.contains("data")) {
                nlohmann::json error_response;
                error_response["status"] = "error";
                error_response["message"] = "Missing data field for write operation";
                return error_response.dump();
            }
            
            // 直接调用write，让模块自己处理只读逻辑
            nlohmann::json result = target->write(request["data"]);
            LOGD("%s write response: %s", targetName.c_str(), result.dump().c_str());
            return result.dump();
        } else {
            nlohmann::json error_response;
            error_response["status"] = "error";
            error_response["message"] = "Invalid mode: " + mode;
            return error_response.dump();
        }

    } catch (const std::exception& e) {
        LOGE("Error processing request: %s", e.what());
        nlohmann::json error_response;
        error_response["status"] = "error";
        error_response["message"] = "Request processing error: " + std::string(e.what());
        return error_response.dump();
    }
}