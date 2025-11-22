/* 动态刷新率的简单实现 */
/* 参考了来自yc9559的Dfps：https://github.com/yc9559/dfps/ */
#ifndef DYNAMIC_FPS
#define DYNAMIC_FPS

#include "JSONSocket/JSONSocket.hpp"
#include "inotifywatcher.hpp"
#include <Alog.hpp>
#include <atomic>
#include <filesystem>
#include <set>
#include <sys/wait.h>
#include <thread>

class DynamicFpsTarget : public ConfigTarget {
private:
    std::shared_ptr<FileWatcher> fileWatcher;
    std::vector<std::string> filePaths;
    std::thread worker_thread_;
    std::atomic<bool> running_;
    int currentfps = 0;

    std::atomic<std::chrono::steady_clock::time_point> _target_time;
    std::atomic<bool> _imrun{false};

    nlohmann::json fpslist;
    mutable std::mutex fpsMutex;

public:
    const std::unordered_map<std::string, std::map<int, int>> allfpsmap;  //带分辨率的列表
    std::atomic<const std::map<int, int>*> fpsmap;                        //当前列表
    std::atomic<int> up_fps{120};
    std::atomic<int> down_fps{60};
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

    DynamicFpsTarget()
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
    }

    void init() {
        if (!running_.exchange(true)) {
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

            change_fps(up_fps.load(std::memory_order_relaxed), (++i) % 10 == 0);
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
            if (!using_backdoor.load(std::memory_order_relaxed)) {
                execute("/system/bin/cmd", "settings", "put", "system", "peak_refresh_rate", std::to_string(fps).c_str());
                execute("/system/bin/cmd", "settings", "put", "system", "min_refresh_rate", std::to_string(fps).c_str());
                execute("/system/bin/cmd", "settings", "put", "system", "miui_refresh_rate", std::to_string(fps).c_str());
                execute("/system/bin/cmd", "settings", "put", "secure", "miui_refresh_rate", std::to_string(fps).c_str());
            } else {
                int id = getFpsId(fps) - 1;
                if (id < 0) {
                    id = 0;
                }

                execute("/system/bin/service", "call", "SurfaceFlinger",
                        std::to_string(backdoorid.load(std::memory_order_relaxed)).c_str(),
                        "i32", std::to_string(id).c_str());
            }
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

    template <typename... Args>
    void execute(Args&&... args) {
        std::vector<const char*> argv = {std::forward<Args>(args)..., nullptr};

        pid_t pid = fork();
        if (pid == 0) {
            execv(argv[0], const_cast<char* const*>(argv.data()));
            _exit(127);
        } else if (pid > 0) {
            waitpid(pid, nullptr, WNOHANG);
        }
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