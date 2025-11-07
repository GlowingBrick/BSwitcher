/*提供一个简单的UnixSocket服务维护*/
/*并且在其中吞吐JSON*/

#include "UnixSocketServer.hpp"

UnixSocketServer::UnixSocketServer(const std::string& socket_path, CallbackFunction callback, 
                                 std::chrono::milliseconds client_timeout)
    : socket_path_(socket_path)
    , callback_(callback)
    , client_timeout_(client_timeout)
    , running_(false) {
    
    LOGD("UnixSocketServer constructor: socket_path=%s", socket_path.c_str());
    
    if (pipe(shutdown_pipe_) == -1) {
        LOGE("Failed to create shutdown pipe: %s", strerror(errno));
    } else {
        LOGD("Shutdown pipe created successfully");
    }
}

UnixSocketServer::~UnixSocketServer() {
    LOGD("UnixSocketServer destructor called");
    stop();
}

bool UnixSocketServer::start() {
    if (running_.exchange(true)) {
        LOGW("Server is already running");
        return false; // 已经在运行
    }

    LOGD("Starting Unix socket server on: %s", socket_path_.c_str());

    // 创建socket
    server_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd_ == -1) {
        LOGE("Failed to create socket: %s", strerror(errno));
        running_ = false;
        return false;
    }
    LOGD("Socket created successfully, fd=%d", server_fd_);

    // 设置socket选项，避免地址占用
    int enable = 1;
    if (setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) < 0) {
        LOGW("Failed to set socket options: %s", strerror(errno));
        // 继续执行，这不是致命错误
    } else {
        LOGD("Socket options set successfully");
    }

    // 绑定地址
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);

    // 移除可能存在的旧socket文件
    unlink(socket_path_.c_str());
    LOGD("Removed existing socket file if any");

    if (bind(server_fd_, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        LOGE("Failed to bind socket: %s", strerror(errno));
        close(server_fd_);
        running_ = false;
        return false;
    }
    LOGD("Socket bound successfully");

    // 开始监听
    if (listen(server_fd_, 128) == -1) { // 增加backlog
        LOGE("Failed to listen on socket: %s", strerror(errno));
        close(server_fd_);
        unlink(socket_path_.c_str());
        running_ = false;
        return false;
    }
    LOGD("Socket listening started with backlog=128");

    server_thread_ = std::thread(&UnixSocketServer::run, this);

    LOGI("Unix socket created: %s", socket_path_.c_str());
    return true;
}

void UnixSocketServer::stop() {
    if (!running_.exchange(false)) {
        LOGD("Server is already stopped");
        return; // 已经停止
    }

    LOGD("Stopping Unix socket server");

    // 通过管道通知监听线程退出
    if (shutdown_pipe_[1] != -1) {
        char dummy = 1;
        write(shutdown_pipe_[1], &dummy, 1);
        LOGD("Shutdown signal sent through pipe");
    }

    if (server_thread_.joinable()) {
        server_thread_.join();
        LOGD("Server thread joined");
    }

    // 关闭所有文件描述符
    if (server_fd_ != -1) {
        close(server_fd_);
        server_fd_ = -1;
        LOGD("Server socket closed");
    }
    
    if (shutdown_pipe_[0] != -1) {
        close(shutdown_pipe_[0]);
        shutdown_pipe_[0] = -1;
    }
    
    if (shutdown_pipe_[1] != -1) {
        close(shutdown_pipe_[1]);
        shutdown_pipe_[1] = -1;
    }
    LOGD("Shutdown pipe closed");

    // 清理socket文件
    unlink(socket_path_.c_str());
    
    LOGI("Unix socket server stopped");
}

bool UnixSocketServer::isRunning() const {
    return running_.load();
}

void UnixSocketServer::run() {
    LOGD("Server listening thread started");
    
    struct pollfd fds[2];
    
    // 设置server socket
    fds[0].fd = server_fd_;
    fds[0].events = POLLIN;
    fds[0].revents = 0;
    
    // 设置关闭管道
    fds[1].fd = shutdown_pipe_[0];
    fds[1].events = POLLIN;
    fds[1].revents = 0;
    
    while (running_) {
        LOGD("Waiting for connection...");
        
        // 阻塞等待事件，直到有连接或关闭信号
        int poll_result = poll(fds, 2, -1);
        
        if (poll_result == -1) {
            if (errno == EINTR) {
                LOGD("Poll interrupted by signal, continuing...");
                continue;
            }
            LOGE("Poll failed: %s", strerror(errno));
            break;
        }
        
        if (poll_result == 0) {
            continue; // 应该不会运行到这里
        }
        
        // 检查关闭信号
        if (fds[1].revents & POLLIN) {
            LOGI("Received shutdown signal");
            break;
        }
        
        // 检查新连接
        if (fds[0].revents & POLLIN) {
            int client_fd = accept(server_fd_, nullptr, nullptr);
            
            if (client_fd == -1) {
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    LOGE("Accept failed: %s", strerror(errno));
                } else {
                    LOGD("Accept would block, continuing...");
                }
                continue;
            }

            LOGD("New client connection accepted, fd=%d", client_fd);
            
            // 在新线程中处理客户端连接
            std::thread client_thread(&UnixSocketServer::handleClient, this, client_fd);
            client_thread.detach();
        }
    }
    
    LOGD("Server listening thread stopped");
}

void UnixSocketServer::handleClient(int client_fd) {
    std::vector<char> buffer(4096);
    std::string received_data;
    auto last_data_time = std::chrono::steady_clock::now();
    bool connection_active = true;
    
    // 设置客户端socket为非阻塞
    int flags = fcntl(client_fd, F_GETFL, 0);
    fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);

    LOGD("Starting to handle client connection, fd=%d", client_fd);

    while (connection_active && running_) {
        // 检查超时：如果长时间没有收到数据，断开连接
        auto current_time = std::chrono::steady_clock::now();
        auto time_since_last_data = std::chrono::duration_cast<std::chrono::milliseconds>(
            current_time - last_data_time);
            
        if (time_since_last_data > client_timeout_) {
            LOGW("Client timeout, closing connection, fd=%d", client_fd);
            break;
        }

        // 使用poll检查是否有数据可读
        struct pollfd pfd;
        pfd.fd = client_fd;
        pfd.events = POLLIN;
        pfd.revents = 0;

        int poll_timeout = std::min(
            static_cast<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                client_timeout_ - time_since_last_data).count()),
            static_cast<int64_t>(100) // 最大poll等待100ms
        );

        if (poll_timeout <= 0) {
            poll_timeout = 1; // 至少等待1ms
        }

        int poll_result = poll(&pfd, 1, poll_timeout);
        
        if (poll_result == -1) {
            LOGE("Poll failed for client fd=%d: %s", client_fd, strerror(errno));
            break;
        } else if (poll_result == 0) {
            // 超时，继续循环检查总超时
            continue;
        }

        if (pfd.revents & POLLIN) {
            ssize_t bytes_received = recv(client_fd, buffer.data(), buffer.size() - 1, 0);
            
            if (bytes_received > 0) {
                // 更新最后收到数据的时间
                last_data_time = std::chrono::steady_clock::now();
                
                // 添加null终止符
                buffer[bytes_received] = '\0';
                received_data.append(buffer.data(), bytes_received);

                LOGD("Received %zd bytes from client fd=%d, total buffer size: %zu", 
                     bytes_received, client_fd, received_data.size());

                bool should_disconnect = processClientData(client_fd, received_data);
                if (should_disconnect) {
                    LOGD("Successfully processed request, closing connection, fd=%d", client_fd);
                    connection_active = false;
                }
                
            } else if (bytes_received == 0) {
                // 客户端正常关闭连接
                LOGD("Client closed connection, fd=%d", client_fd);
                connection_active = false;
            } else {
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    LOGE("Receive failed for client fd=%d: %s", client_fd, strerror(errno));
                    connection_active = false;
                } else {
                    LOGD("Receive would block for client fd=%d", client_fd);
                }
            }
        } else if (pfd.revents & (POLLHUP | POLLERR)) {
            // 连接错误或挂起
            LOGW("Connection error or hangup for client fd=%d", client_fd);
            connection_active = false;
        }
    }

    close(client_fd);
    LOGD("Client connection closed, fd=%d", client_fd);
}

bool UnixSocketServer::processClientData(int client_fd, std::string& received_data) {
    // 跳过前导空白字符
    size_t start_pos = 0;
    while (start_pos < received_data.length() && 
          std::isspace(static_cast<unsigned char>(received_data[start_pos]))) {
        start_pos++;
    }
    
    if (start_pos >= received_data.length()) {
        received_data.clear();
        LOGD("Received only whitespace data, clearing buffer");
        return false; // 继续等待数据
    }

    try {
        // 尝试解析JSON
        nlohmann::json json;
        size_t bytes_parsed = 0;
        
        try {
            json = nlohmann::json::parse(received_data.begin() + start_pos, received_data.end());
            // 计算解析消耗的字节数
            std::string json_str = json.dump();
            bytes_parsed = start_pos + json_str.length();
            
            LOGD("Successfully parsed JSON, bytes_parsed=%zu, json_keys_count=%zu", 
                 bytes_parsed, json.size());
                 
        } catch (const nlohmann::json::parse_error& e) {
            if (e.id == 101) { // parse error: unexpected end of input
                // 不完整的JSON，等待更多数据
                LOGD("Incomplete JSON received, waiting for more data");
                return false;
            } else {
                // 其他JSON错误，抛出异常
                throw;
            }
        }

        LOGD("Calling callback function with parsed JSON");
        std::string response = callback_(json); //进入回调函数
        
        // 确保响应以换行符结束
        if (!response.empty() && response.back() != '\n') {
            response += '\n';
        }
        
        LOGD("Sending response to client, response_length=%zu", response.length());
        // 发送响应
        sendResponse(client_fd, response);
        
        return true;        //完毕断开
        
    } catch (const nlohmann::json::parse_error& e) {
        LOGE("JSON parse error for client fd=%d: %s", client_fd, e.what());
        
        nlohmann::json error_json;
        error_json["error"] = "Invalid JSON format";
        std::string error_response = error_json.dump();
        if (!error_response.empty() && error_response.back() != '\n') {
            error_response += '\n';
        }
        sendResponse(client_fd, error_response);
        
        // JSON解析错误
        received_data.clear();
        return false;
        
    } catch (const std::exception& e) {
        LOGE("Error processing message for client fd=%d: %s", client_fd, e.what());
        
        nlohmann::json error_json;
        error_json["error"] = "Internal server error";
        std::string error_response = error_json.dump();
        if (!error_response.empty() && error_response.back() != '\n') {
            error_response += '\n';
        }
        sendResponse(client_fd, error_response);
        
        // 致命错误，断开连接
        return true;
    }
}

void UnixSocketServer::sendResponse(int client_fd, const std::string& response) {   //发送响应
    if (response.empty()) {
        LOGD("Empty response, nothing to send");
        return;
    }

    size_t total_sent = 0;
    
    while (total_sent < response.length() && running_) {
        ssize_t bytes_sent = send(client_fd, 
                                response.c_str() + total_sent, 
                                response.length() - total_sent, 
                                MSG_NOSIGNAL);
        
        if (bytes_sent == -1) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                LOGE("Send failed for client fd=%d: %s", client_fd, strerror(errno));
                break;
            }
            LOGD("Send would block for client fd=%d, retrying...", client_fd);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        
        total_sent += bytes_sent;
        LOGD("Sent %zd bytes to client fd=%d, total_sent=%zu/%zu", 
             bytes_sent, client_fd, total_sent, response.length());
    }
    
    if (total_sent == response.length()) {
        LOGD("Successfully sent complete response to client fd=%d", client_fd);
    } else {
        LOGW("Incomplete response sent to client fd=%d: %zu/%zu bytes", 
             client_fd, total_sent, response.length());
    }
}

void UnixSocketServer::sendErrorResponse(int client_fd, const std::string& error_message) { //发送错误
    LOGD("Sending error response to client fd=%d: %s", client_fd, error_message.c_str());
    
    nlohmann::json error_json;
    error_json["error"] = error_message;
    
    std::string response = error_json.dump();
    if (!response.empty() && response.back() != '\n') {
        response += '\n';
    }
    
    sendResponse(client_fd, response);
}