/* 动态刷新率的简单实现 */
/* 参考了来自yc9559的Dfps：https://github.com/yc9559/dfps/ */
#ifndef DYNAMIC_FPS
#define DYNAMIC_FPS

#include "JSONSocket/JSONSocket.hpp"
#include "inotifywatcher.hpp"
#include <Alog.hpp>
#include <atomic>
#include <cerrno>
#include <condition_variable>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

/*
    命令执行器：解耦刷新率切换与外部命令执行

    change_fps 只负责把一批命令提交给这里，由独立线程串行执行并阻塞回收。
    这样做有三个原因：
      1. 阻塞 waitpid 才能真正回收子进程。原先在提交侧用 WNOHANG，
         调用瞬间子进程尚未退出，waitpid 立即返回 0 且不再重试，
         导致每执行一条命令泄漏一个僵尸进程。
      2. 阻塞 waitpid 不能放在触摸热路径上（单条 cmd settings put
         可达数十至数百毫秒），故移入独立线程。
      3. 串行执行保证"最后提交的最后生效"。若并发执行，多条
         settings put 的完成顺序不确定，快速滑动时旧帧率可能后落盘。

    新提交整批覆盖旧的未执行提交：滑动过程中被跳过的中间帧率无需下发，
    顺带减轻对 system_server 的冲击。
*/
class CommandExecutor {
public:
    using Command = std::vector<std::string>;
    using Batch = std::vector<Command>;

    ~CommandExecutor() {
        stop();
    }

    void start() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (worker_.joinable()) {
            return;
        }
        stopping_ = false;
        worker_ = std::thread(&CommandExecutor::loop, this);
    }

    // 等待当前批次执行完毕后退出，不打断已在运行的命令
    void stop() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!worker_.joinable()) {
                return;
            }
            stopping_ = true;
        }
        cv_.notify_all();
        worker_.join();
    }

    // 提交一批命令，覆盖尚未执行的旧提交。对调用方几乎无阻塞
    void submit(Batch batch) {
        if (batch.empty()) {
            return;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_ || !worker_.joinable()) {  //未运行时丢弃，避免堆积永不执行的提交
                return;
            }
            pending_ = std::move(batch);
        }
        cv_.notify_one();
    }

private:
    void loop() {
        for (;;) {
            Batch batch;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                //条件变量阻塞等待，空闲时不占用CPU
                cv_.wait(lock, [this]() { return pending_.has_value() || stopping_; });

                if (!pending_) {  //仅在 stopping_ 且无待执行批次时退出
                    return;
                }
                batch = std::move(*pending_);
                pending_.reset();
            }

            for (const auto& command : batch) {
                run(command);
            }
        }
    }

    static void run(const Command& command) {
        if (command.empty()) {
            return;
        }

        //argv 必须在 fork 之前构造完毕。
        //fork 出的子进程只有当前线程，若此时其他线程持有 malloc 锁，
        //子进程内再做堆分配会死锁，因此 child 中只允许 execv/_exit。
        std::vector<char*> argv;
        argv.reserve(command.size() + 1);
        for (const auto& arg : command) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);

        pid_t pid = fork();
        if (pid == 0) {
            execv(argv[0], argv.data());
            _exit(127);
        } else if (pid < 0) {
            LOGE("Failed to fork for %s: %s", argv[0], strerror(errno));
            return;
        }

        int status;
        while (waitpid(pid, &status, 0) < 0) {
            if (errno != EINTR) {  //被信号打断需重试，否则漏回收
                LOGE("waitpid failed for pid %d: %s", pid, strerror(errno));
                return;
            }
        }
    }

    std::thread worker_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::optional<Batch> pending_;
    bool stopping_ = false;
};

class DynamicFpsTarget : public ConfigTarget {
private:
    std::shared_ptr<FileWatcher> fileWatcher;
    std::vector<std::string> filePaths;
    std::thread worker_thread_;
    std::atomic<bool> running_{false};
    int currentfps = 0;

    std::atomic<std::chrono::steady_clock::time_point> _target_time;
    std::atomic<bool> _imrun{false};

    nlohmann::json fpslist;
    mutable std::mutex fpsMutex;

    CommandExecutor executor;

    bool* fpsforce;

public:
    const std::unordered_map<std::string, std::map<int, int>> allfpsmap;  //带分辨率的列表
    std::atomic<const std::map<int, int>*> fpsmap;                        //当前列表
    std::atomic<int> up_fps{120};
    std::atomic<int> down_fps{60};
    std::atomic<int> lowbri{-10};
    std::atomic<int> currentbri{1000};

    std::atomic<int> down_during_ms{2500};

    std::atomic<int> backdoorid{1035};
    std::atomic<bool> using_backdoor{false};

    std::string getName() const override {
        return "dynamicFps";
    }

    nlohmann::json read() override {
        return fpslist;
    }

    nlohmann::json write(const nlohmann::json& jsonData) override {
        return {{"status", "error"}, {"message", "DynamicFps target is read-only"}};
    }

    DynamicFpsTarget(bool* fps_force)
        : allfpsmap(getResolutionToDisplayModes()) {
        std::vector<std::string> filePaths;
        try {
            for (const auto& entry : std::filesystem::directory_iterator("/dev/input")) {
                if (entry.is_character_file()) {
                    filePaths.push_back(entry.path().string());
                }
            }
        } catch (const std::filesystem::filesystem_error& ex) {
            LOGE("Failed to load input, DynamicFps could not be enabled.");
            return;
        }
        fileWatcher = std::make_shared<FileWatcher>(filePaths, 300, IN_ACCESS);  //一些设备使用IN_MODIFY没有响应

        fpslist = getAvailableRefreshRates();

        fpsmap = &allfpsmap.begin()->second;  //随便指一个

        fpsforce = fps_force;
    }

    void init() {
        if (!running_.exchange(true)) {
            executor.start();  //必须先于工作线程启动，否则首批提交会被丢弃
            worker_thread_ = std::thread(&DynamicFpsTarget::work, this);
            LOGD("DynamicFpsTarget Started");
        }
    }

    void stop() {
        if (running_.exchange(false)) {
            LOGD("DynamicFpsTarget Stoped");
            fileWatcher->cleanup();  //cleanup会立即触发wait
            if (worker_thread_.joinable()) {
                worker_thread_.join();
            }
            executor.stop();  //待工作线程退出后再停，等当前批次执行完毕
        }
    }

    std::string findMax() {  //找出最多帧率选择且最大分辨率
        auto max_it = allfpsmap.begin();
        for (auto it = allfpsmap.begin(); it != allfpsmap.end(); ++it) {
            if (it->second.size() > max_it->second.size()) {  //默认配置为数量最多的项
                max_it = it;
            } else if (it->second.size() == max_it->second.size()) {  //相同数量配置为分辨率大的项
                if (DynamicFpsTarget::countPixel(it->first) > DynamicFpsTarget::countPixel(max_it->first)) {
                    max_it = it;
                }
            }
        }
        return max_it->first;
    }

private:
    void work() {
        fileWatcher->initialize();
        int i = 0;
        while (true) {

            std::this_thread::sleep_for(std::chrono::milliseconds(300));

            fileWatcher->wait(-1);
            if (!running_.load(std::memory_order_relaxed)) {
                return;
            }
            if (lowbri.load(std::memory_order_relaxed) <= currentbri.load(std::memory_order_relaxed)) {
                change_fps(up_fps.load(std::memory_order_relaxed), ((++i) % 15 == 0) || *fpsforce);
            } else {
                change_fps(60, ((++i) % 15 == 0) || *fpsforce);  //低亮度锁60
            }
            waitfor_downfps(down_during_ms.load(std::memory_order_relaxed));
        }
    }

    void waitfor_downfps(int initialDelayMs) {
        _target_time.store(std::chrono::steady_clock::now() +
                               std::chrono::milliseconds(initialDelayMs),
                           std::memory_order_relaxed);

        if (!_imrun.exchange(true)) {  //定时线程
            std::thread([this]() {
                while (std::chrono::steady_clock::now() < _target_time.load(std::memory_order_relaxed)) {
                    std::this_thread::sleep_until(_target_time.load(std::memory_order_relaxed));
                }
                _imrun.store(false);
                change_fps(down_fps.load(std::memory_order_relaxed));
            }).detach();
        }
    }

    void change_fps(int fps, bool force = false) {
        std::lock_guard<std::mutex> lock(fpsMutex);
        if (currentfps != fps || force) {
            currentfps = fps;
            LOGD("Frame rate changed to %d", fps);

            CommandExecutor::Batch batch;
            if (!using_backdoor.load(std::memory_order_relaxed)) {
                const std::string fpsStr = std::to_string(fps);
                batch = {
                    {"/system/bin/cmd", "settings", "put", "system", "peak_refresh_rate", fpsStr},
                    {"/system/bin/cmd", "settings", "put", "system", "min_refresh_rate", fpsStr},
                    {"/system/bin/cmd", "settings", "put", "system", "miui_refresh_rate", fpsStr},
                    {"/system/bin/cmd", "settings", "put", "secure", "miui_refresh_rate", fpsStr}};
            } else {
                int id = getFpsId(fps) - 1;
                if (id < 0) {
                    id = 0;
                }

                batch = {
                    {"/system/bin/service", "call", "SurfaceFlinger",
                     std::to_string(backdoorid.load(std::memory_order_relaxed)),
                     "i32", std::to_string(id)}};
            }

            executor.submit(std::move(batch));  //整批提交，覆盖未执行的旧提交
        }
    }

    int getFpsId(int key) {  //尝试选择一个最接近的fpsid.不保证map与vector完全对应
        auto tfpsmap = fpsmap.load(std::memory_order_relaxed);

        auto exact_it = (*tfpsmap).find(key);
        if (exact_it != (*tfpsmap).end()) {
            return exact_it->second;
        }

        if ((*tfpsmap).empty()) {
            return 0;
        }

        auto it = (*tfpsmap).lower_bound(key);

        if (it == (*tfpsmap).end()) {
            return (*tfpsmap).rbegin()->second;
        }

        if (it == (*tfpsmap).begin()) {

            return it->second;
        }
        auto prev_it = std::prev(it);

        int diff_prev = std::abs(key - prev_it->first);
        int diff_next = std::abs(it->first - key);
        return (diff_prev <= diff_next) ? prev_it->second : it->second;
    }

public:
    static std::vector<int> getAvailableRefreshRates() {  //获取所有可用的刷新率
        std::set<int> rates;

        std::unique_ptr<FILE, decltype(&pclose)> pipe(popen("dumpsys display | grep DisplayModeRecord", "r"), pclose);
        if (!pipe) return {};

        std::string output;
        char buffer[256];
        while (fgets(buffer, sizeof(buffer), pipe.get())) output += buffer;

        auto parseRate = [](const std::string& str) -> int {
            std::string cleanStr = str;
            cleanStr.erase(0, cleanStr.find_first_not_of(" \t\r\n"));
            cleanStr.erase(cleanStr.find_last_not_of(" \t\r\n") + 1);

            if (cleanStr.empty()) {
                return 0;
            }

            try {
                double fps = std::stod(cleanStr);
                int rounded = std::round(fps);
                if (fps < 1.0 || fps > 512.0) {
                    return 0;
                }
                return std::abs(fps - rounded) < 0.01 ? rounded : static_cast<int>(fps);
            } catch (...) {
                return 0;
            }
        };

        size_t pos = 0;
        while (pos < output.size()) {
            if (auto fpsPos = output.find("fps=", pos); fpsPos != std::string::npos) {  //fps
                auto start = fpsPos + 4;
                auto end = output.find_first_of(",}", start);
                if (end != std::string::npos) {
                    std::string rateStr = output.substr(start, end - start);
                    if (int rate = parseRate(rateStr); rate > 0) {
                        rates.insert(rate);
                    }
                }
                pos = fpsPos + 1;
            } else if (auto altPos = output.find("alternativeRefreshRates=[", pos); altPos != std::string::npos) {  // alternativeRefreshRates
                auto start = altPos + 26;
                auto end = output.find("]", start);
                if (end != std::string::npos) {
                    std::string altList = output.substr(start, end - start);
                    std::istringstream iss(altList);
                    std::string token;

                    while (std::getline(iss, token, ',')) {
                        if (int rate = parseRate(token); rate > 0) {
                            rates.insert(rate);
                        }
                    }
                }
                pos = altPos + 1;
            } else {
                break;
            }
        }

        std::vector<int> result;
        for (int rate : rates) {
            if (rate >= 10 && rate <= 512) {
                result.push_back(rate);
            }
        }

        std::sort(result.begin(), result.end());
        return result;
    }

    /*可能的内容：
    # dumpsys display | grep DisplayModeRecord   
      DisplayModeRecord{mMode={id=1, width=1800, height=2880, fps=120.00001, alternativeRefreshRates=[30.000002, 48.000004, 50.0, 60.000004, 90.0, 144.00002]}}
      DisplayModeRecord{mMode={id=2, width=1800, height=2880, fps=144.00002, alternativeRefreshRates=[30.000002, 48.000004, 50.0, 60.000004, 90.0, 120.00001]}}
      DisplayModeRecord{mMode={id=3, width=1800, height=2880, fps=90.0, alternativeRefreshRates=[30.000002, 48.000004, 50.0, 60.000004, 120.00001, 144.00002]}}
      DisplayModeRecord{mMode={id=4, width=1800, height=2880, fps=60.000004, alternativeRefreshRates=[30.000002, 48.000004, 50.0, 90.0, 120.00001, 144.00002]}}
      DisplayModeRecord{mMode={id=5, width=1800, height=2880, fps=50.0, alternativeRefreshRates=[30.000002, 48.000004, 60.000004, 90.0, 120.00001, 144.00002]}}
      DisplayModeRecord{mMode={id=6, width=1800, height=2880, fps=48.000004, alternativeRefreshRates=[30.000002, 50.0, 60.000004, 90.0, 120.00001, 144.00002]}}
      DisplayModeRecord{mMode={id=7, width=1800, height=2880, fps=30.000002, alternativeRefreshRates=[48.000004, 50.0, 60.000004, 90.0, 120.00001, 144.00002]}}
      DisplayModeRecord{mMode={id=19, width=1920, height=1080, fps=60.000004, alternativeRefreshRates=[]}}
      DisplayModeRecord{mMode={id=20, width=1680, height=1050, fps=60.000004, alternativeRefreshRates=[]}}
      DisplayModeRecord{mMode={id=21, width=1600, height=900, fps=60.000004, alternativeRefreshRates=[]}}
      DisplayModeRecord{mMode={id=22, width=1280, height=1024, fps=60.000004, alternativeRefreshRates=[]}}
      DisplayModeRecord{mMode={id=23, width=1440, height=900, fps=60.000004, alternativeRefreshRates=[]}}
      DisplayModeRecord{mMode={id=24, width=1280, height=720, fps=60.000004, alternativeRefreshRates=[50.0]}}
      DisplayModeRecord{mMode={id=25, width=1280, height=720, fps=50.0, alternativeRefreshRates=[60.000004]}}
      DisplayModeRecord{mMode={id=26, width=1024, height=768, fps=60.000004, alternativeRefreshRates=[]}}
      DisplayModeRecord{mMode={id=27, width=800, height=600, fps=60.000004, alternativeRefreshRates=[]}}
      DisplayModeRecord{mMode={id=28, width=720, height=480, fps=60.000004, alternativeRefreshRates=[]}}
      DisplayModeRecord{mMode={id=29, width=640, height=480, fps=60.000004, alternativeRefreshRates=[]}}
      数据结构：
      {
        "1800x2880": {
            {30, 7},
            {48, 6},
             ...
        },
        "1080x2400": {
            {60, 8},
            {90, 9},
            {120, 10}
            ......
        },
        .......
    }

    */

    static std::unordered_map<std::string, std::map<int, int>> getResolutionToDisplayModes() {  //解析所有显示模式
        std::unordered_map<std::string, std::map<int, int>> resolutionModes;

        std::unique_ptr<FILE, decltype(&pclose)> pipe(popen("dumpsys display | grep DisplayModeRecord", "r"), pclose);
        if (!pipe) return {};

        std::string output;
        char buffer[256];
        while (fgets(buffer, sizeof(buffer), pipe.get())) output += buffer;

        auto parseFps = [](const std::string& str) -> int {
            std::string cleanStr = str;
            cleanStr.erase(0, cleanStr.find_first_not_of(" \t\r\n"));
            cleanStr.erase(cleanStr.find_last_not_of(" \t\r\n") + 1);

            if (cleanStr.empty()) {
                return 0;
            }

            try {
                double fps = std::stod(cleanStr);
                int rounded = std::round(fps);
                if (fps < 1.0 || fps > 512.0) {
                    return 0;
                }
                return std::abs(fps - rounded) < 0.01 ? rounded : static_cast<int>(fps);
            } catch (...) {
                return 0;
            }
        };

        std::istringstream iss(output);
        std::string line;

        while (std::getline(iss, line)) {
            if (line.find("DisplayModeRecord") == std::string::npos) {
                continue;
            }

            int id = 0, width = 0, height = 0, fps = 0;

            auto idPos = line.find("id=");
            if (idPos != std::string::npos) {
                auto idStart = idPos + 3;
                auto idEnd = line.find_first_of(", ", idStart);
                if (idEnd != std::string::npos) {
                    try {
                        id = std::stoi(line.substr(idStart, idEnd - idStart));
                    } catch (...) {
                        continue;
                    }
                }
            }

            auto widthPos = line.find("width=");
            if (widthPos != std::string::npos) {
                auto widthStart = widthPos + 6;
                auto widthEnd = line.find_first_of(", ", widthStart);
                if (widthEnd != std::string::npos) {
                    try {
                        width = std::stoi(line.substr(widthStart, widthEnd - widthStart));
                    } catch (...) {
                        continue;
                    }
                }
            }

            auto heightPos = line.find("height=");
            if (heightPos != std::string::npos) {
                auto heightStart = heightPos + 7;
                auto heightEnd = line.find_first_of(", ", heightStart);
                if (heightEnd != std::string::npos) {
                    try {
                        height = std::stoi(line.substr(heightStart, heightEnd - heightStart));
                    } catch (...) {
                        continue;
                    }
                }
            }

            auto fpsPos = line.find("fps=");
            if (fpsPos != std::string::npos) {  // 解析fps
                auto fpsStart = fpsPos + 4;
                auto fpsEnd = line.find_first_of(",}", fpsStart);
                if (fpsEnd != std::string::npos) {
                    std::string fpsStr = line.substr(fpsStart, fpsEnd - fpsStart);
                    fps = parseFps(fpsStr);
                }
            }

            if (id > 0 && width > 0 && height > 0 && fps > 0) {
                std::string resolution = std::to_string(width) + "x" + std::to_string(height);
                resolutionModes[resolution][fps] = id;
                LOGD("Found: %s,%d,%d", resolution.c_str(), fps, id);
            }
        }

        return resolutionModes;
    }

    static std::pair<int, int> parseResolution(const std::string& resolution_str) {  //从hxw字符串解析出h和w
        size_t x_pos = resolution_str.find('x');
        if (x_pos == std::string::npos || x_pos == 0 || x_pos == resolution_str.length() - 1) {
            return {0, 0};
        }

        try {
            int width = std::stoi(resolution_str.substr(0, x_pos));
            int height = std::stoi(resolution_str.substr(x_pos + 1));
            return {width, height};
        } catch (const std::exception&) {
            return {0, 0};
        }
    }

    static int countPixel(const std::string& resolution_str) {
        auto t = parseResolution(resolution_str);
        return t.first * t.second;
    }
};

#endif