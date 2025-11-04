#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

class UnixSocketClient {
private:
    int sockfd;
    struct sockaddr_un server_addr;

public:
    UnixSocketClient(const std::string& socket_path) {
        // 创建socket
        sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (sockfd < 0) {
            throw std::runtime_error("无法创建socket");
        }

        // 设置socket地址
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sun_family = AF_UNIX;
        strncpy(server_addr.sun_path, socket_path.c_str(), sizeof(server_addr.sun_path) - 1);
    }

    bool connect() {
        if (::connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            return false;
        }
        return true;
    }

    bool send(const std::string& data) {
        ssize_t bytes_sent = ::send(sockfd, data.c_str(), data.length(), 0);
        return bytes_sent == static_cast<ssize_t>(data.length());
    }

    std::string receive() {
        std::vector<char> buffer(4096);
        std::string response;
        
        // 设置接收超时（5秒）
        struct timeval tv;
        tv.tv_sec = 5;
        tv.tv_usec = 0;
        setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        while (true) {
            ssize_t bytes_received = recv(sockfd, buffer.data(), buffer.size() - 1, 0);
            if (bytes_received <= 0) {
                break;
            }
            buffer[bytes_received] = '\0';
            response.append(buffer.data(), bytes_received);
        }
        
        return response;
    }

    ~UnixSocketClient() {
        if (sockfd >= 0) {
            close(sockfd);
        }
    }
};

//较旧的系统中nc -U 不可用，用此程序临时替代
// echo "{\"target\":\"scheduler\",\"mode\": \"read\"}" | ./unixsoc /dev/BSwitcher
int main(int argc, char* argv[]) {
    // 检查参数
    if (argc != 2) {
        std::cerr << "用法: " << argv[0] << " <socket_path>" << std::endl;
        return 1;
    }

    std::string socket_path = argv[1];

    try {
        // 从标准输入读取数据
        std::string input_data;
        std::string line;
        
        while (std::getline(std::cin, line)) {
            input_data += line + "\n";
        }

        // 如果没有任何输入，直接退出
        if (input_data.empty()) {
            return 0;
        }

        // 创建socket客户端并连接
        UnixSocketClient client(socket_path);
        
        if (!client.connect()) {
            std::cerr << "无法连接到socket: " << socket_path << std::endl;
            return 1;
        }

        // 发送数据
        if (!client.send(input_data)) {
            std::cerr << "发送数据失败" << std::endl;
            return 1;
        }

        // 接收响应
        std::string response = client.receive();
        
        if (!response.empty()) {
            std::cout << response;
        }

    } catch (const std::exception& e) {
        std::cerr << "错误: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}