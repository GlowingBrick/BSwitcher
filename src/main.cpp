#include "Alog.hpp"
#include "BSwitcher.hpp"
#include <fstream>
#include <libgen.h>
#include <string>
#define unlikely(x) __builtin_expect(!!(x), 0)

std::unique_ptr<BSwitcher> bswitcher;

void bind_to_core() {  //绑定到0 1小核
    cpu_set_t mask;
    CPU_ZERO(&mask);
    CPU_SET(0, &mask);
    CPU_SET(1, &mask);

    sched_setaffinity(0, sizeof(mask), &mask);
}

bool loadStaticData() {                      //尝试加载静态数据
    std::ifstream file("static_data.json");  //如果存在static_data.json
    if (!file.is_open()) {
        return false;
    }
    LOGD("Trying to use static data...");
    try {
        nlohmann::json json_data = nlohmann::json::parse(file);

        if (json_data.contains("enable") && !json_data["enable"].get<bool>()) {  //是否enable
            LOGI("Static mode disabled");
            return false;
        }

        if (!json_data.contains("name") ||
            !json_data.contains("author") ||
            !json_data.contains("version") ||
            !json_data.contains("entry")) {
            LOGW("Static mode not enabled: missing critical fields");
            return false;
        }

        static_data data;
        data.name = json_data["name"].get<std::string>();
        data.author = json_data["author"].get<std::string>();
        data.version = json_data["version"].get<std::string>();
        data.entry = json_data["entry"].get<std::string>();

        bswitcher = std::make_unique<BSwitcher>(data);
        LOGD("Using static data");
        return true;

    } catch (const std::exception& e) {
        LOGW("cstatic_data.json could not be parsed");
        return false;
    }
}

int main() {

    if (fork() > 0) {
        exit(0);
    }

    setsid();

    if (fork() > 0) {
        exit(0);  // 孤儿模拟器.jpg
    }

    umask(0);

    char exePath[PATH_MAX];
    ssize_t count = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);

    if (count != -1) {
        exePath[count] = '\0';
        char* dirPath = dirname(exePath);

        chdir(dirPath);
    }

    int dev_null_fd = open("/dev/null", O_RDWR);
    dup2(dev_null_fd, STDIN_FILENO);
    dup2(dev_null_fd, STDOUT_FILENO);
    dup2(dev_null_fd, STDERR_FILENO);
    close(dev_null_fd);

    LOGI("BSwitcher is preparing...");

    if (!loadStaticData()) {
        bswitcher = std::make_unique<BSwitcher>();
    }

    if (!bswitcher->init()) {  //初始化
        LOGE("Failed to initialize");
        return -1;
    }

    bind_to_core();
    LOGD("Preparing to enter main loop");
    bswitcher->main_loop();
    LOGE("Main function abnormal exit");
    exit(-1);
    return 0;
}