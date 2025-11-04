/*提供一个简单的UnixSocket服务维护*/
/*并且在其中吞吐JSON*/

#ifndef UNIX_SOCKET_SERVER_HPP
#define UNIX_SOCKET_SERVER_HPP

#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <chrono>
#include <system_error>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <cstring>
#include "Alog.hpp"
#include <nlohmann/json.hpp>

class UnixSocketServer {
public:
    using CallbackFunction = std::function<std::string(const nlohmann::json&)>;

    UnixSocketServer(const std::string& socket_path, CallbackFunction callback, 
                    std::chrono::milliseconds client_timeout = std::chrono::seconds(30));

    ~UnixSocketServer();

    // 禁用拷贝和移动语义
    UnixSocketServer(const UnixSocketServer&) = delete;
    UnixSocketServer& operator=(const UnixSocketServer&) = delete;
    UnixSocketServer(UnixSocketServer&&) = delete;
    UnixSocketServer& operator=(UnixSocketServer&&) = delete;

    bool start();
    void stop();
    bool isRunning() const;

private:
    void run();
    void handleClient(int client_fd);
    bool processClientData(int client_fd, std::string& received_data);
    void sendResponse(int client_fd, const std::string& response);
    void sendErrorResponse(int client_fd, const std::string& error_message);

private:
    std::string socket_path_;
    CallbackFunction callback_;
    std::chrono::milliseconds client_timeout_;
    std::atomic<bool> running_;
    std::thread server_thread_;
    int server_fd_{-1};
    int shutdown_pipe_[2]{-1, -1}; // 用于优雅关闭的管道
};

#endif // UNIX_SOCKET_SERVER_HPP