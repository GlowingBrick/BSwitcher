#include "Alog.hpp"
#include "BSwitcher.hpp"
#include <fstream>
#include <libgen.h>
#include <signal.h>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
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

int worker_process(const std::string& runningPath) {
    // 设置工作目录
    if (runningPath.empty()) {
        char exePath[PATH_MAX];
        ssize_t count = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);

        if (count != -1) {
            exePath[count] = '\0';
            char* dirPath = dirname(exePath);
            chdir(dirPath);
        }
    } else {
        chdir(runningPath.c_str());
    }

    int dev_null_fd = open("/dev/null", O_RDWR);
    dup2(dev_null_fd, STDIN_FILENO);
    dup2(dev_null_fd, STDOUT_FILENO);
    dup2(dev_null_fd, STDERR_FILENO);
    close(dev_null_fd);

    LOGI("BSwitcher worker process is preparing...");

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
    LOGE("Worker process abnormal exit");
    return -1;
}

// 检查进程是否存在
bool is_process_alive(pid_t pid) {
    if (pid <= 0) return false;

    if (kill(pid, 0) == 0) {
        return true;
    }

    if (errno == ESRCH) {
        return false;
    } else if (errno == EPERM) {
        return true;
    }

    return true;
}


// 守护进程函数
void daemon_process(const std::string& runningPath) {
    LOGI("Daemon process started, monitoring worker process...");

    while (true) {
        pid_t worker_pid = fork();

        if (worker_pid == 0) {
            _exit(worker_process(runningPath)); //子进程
        } else if (worker_pid > 0) {
            LOGI("Worker process started with PID: %d", worker_pid);

            while (true) {
                int status;
                pid_t result = waitpid(worker_pid, &status, 0);

                if (result > 0) {
                    if (WIFEXITED(status)) {
                        int exit_code = WEXITSTATUS(status);
                        LOGW("Worker process exited with code: %d", exit_code);
                    } else if (WIFSIGNALED(status)) {
                        int signal_num = WTERMSIG(status);
                        LOGW("Worker process killed by signal: %d", signal_num);
                    }

                if (!is_process_alive(worker_pid)) {
                        break; //准备重启
                    } else {
                        // 理论上不会运行到这，但不确定会不会抽风
                        LOGW("Worker process %d still alive after waitpid, continuing to wait...", worker_pid);
                        continue;
                    }
                } else if (result == 0) {// 理论上不会运行到这，但不确定会不会抽风
                    LOGW("waitpid returned 0 in blocking mode, checking process status...");

                    if (!is_process_alive(worker_pid)) {
                        LOGW("Worker process %d appears to have died", worker_pid);
                        break; 
                    } else {
                        LOGW("Worker process %d is still alive, continuing to wait...", worker_pid);
                        continue; 
                    }
                } else {    // 错误
                    LOGW("waitpid failed for worker process %d (errno: %d), checking process status...",
                         worker_pid, errno);

                    if (errno == ECHILD) {  //不存在子进程
                        LOGW("No child process found, worker process %d has exited", worker_pid);
                        break;
                    } else {
                        if (!is_process_alive(worker_pid)) {
                            LOGW("Worker process %d appears to have died", worker_pid);
                            break; 
                        } else {
                            LOGW("Worker process %d is still alive despite waitpid error, continuing...", worker_pid);
                            continue;
                        }
                    }
                }
            }

            LOGW("Waiting 3 seconds before restarting worker process...");
            sleep(3);
        } else {
            // fork失败
            LOGE("Failed to fork worker process, waiting 5 seconds before retry...");
            sleep(5);
        }
    }
}


int main(int argc, char* argv[]) {
    std::string runningPath = "";
    int opt;
    while ((opt = getopt(argc, argv, "p:")) != -1) {  //-p接受运行环境
        switch (opt) {
        case 'p':
            runningPath = optarg;
            break;
        }
    }

    if (fork() > 0) {
        exit(0);
    }

    setsid();

    if (fork() > 0) {
        exit(0);
    }

    umask(0);

    pid_t pid = fork();
    if (pid == 0) {
        daemon_process(runningPath);
        _exit(0);
    } else if (pid > 0) {
        exit(0);
    } else {
        LOGE("Failed to fork daemon process");
        return -1;
    }

    return 0;
}