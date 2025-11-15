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
    std::atomic<int> currentfps = 0;

    std::atomic<std::chrono::steady_clock::time_point> _target_time;
    std::atomic<bool> _imrun{false};

    std::map<int, int> fpsmap;

    nlohmann::json fpslist;

public:
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

    DynamicFpsTarget() {
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
        fileWatcher = std::make_shared<FileWatcher>(filePaths, 300, IN_ACCESS); //一些设备使用IN_MODIFY没有响应

        fpslist = getAvailableRefreshRates();
        fpsmap = getDisplayModeIdToFps();
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
            fileWatcher->cleanup(); //cleanup会立即触发wait
            if (worker_thread_.joinable()) {
                worker_thread_.join();
            }
        }
    }

private:
    void work() {
        fileWatcher->initialize();

        while (true) {

            std::this_thread::sleep_for(std::chrono::milliseconds(300));

            fileWatcher->wait(-1);
            if (!running_.load(std::memory_order_relaxed)) {
                return;
            }

            change_fps(up_fps.load(std::memory_order_relaxed));
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

    void change_fps(int fps) {
        if (currentfps.exchange(fps, std::memory_order_relaxed) != fps) {
            LOGD("Frame rate changed to %d", fps);
            if(!using_backdoor.load(std::memory_order_relaxed)){
                execute("/system/bin/cmd", "settings", "put", "system", "peak_refresh_rate", std::to_string(fps).c_str());
                execute("/system/bin/cmd", "settings", "put", "system", "min_refresh_rate", std::to_string(fps).c_str());
                execute("/system/bin/cmd", "settings", "put", "system", "miui_refresh_rate", std::to_string(fps).c_str());
                execute("/system/bin/cmd", "settings", "put", "secure", "miui_refresh_rate", std::to_string(fps).c_str());
            }else{
                int id=getFpsId(fps)-1;
                if(id<0){id=0;}

                execute("/system/bin/service", "call", "SurfaceFlinger", 
                    std::to_string(backdoorid.load(std::memory_order_relaxed)).c_str(), 
                    "i32", std::to_string(id).c_str());
            }

        }
    }

    int getFpsId(int key) { //尝试选择一个最接近的fpsid.不保证map与vector
        auto exact_it = fpsmap.find(key);
        if (exact_it != fpsmap.end()) {
            return exact_it->second;  
        }
        
        if (fpsmap.empty()) {
            return 120; 
        }
        
        auto it = fpsmap.lower_bound(key);
        
        if (it == fpsmap.end()) {
            return fpsmap.rbegin()->second;
        }
        
        if (it == fpsmap.begin()) {

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

    static std::map<int, int> getDisplayModeIdToFps() {
        //记录id与fps对应关系。用于service call SurfaceFlinger 1035 i32 [id-1]
        //似乎是这样
        std::map<int, int> idToFps;

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

        size_t pos = 0;
        while (pos < output.size()) {

            auto idPos = output.find("id=", pos);  // 查找id
            if (idPos == std::string::npos) break;

            auto fpsPos = output.find("fps=", idPos);  // 查找fps
            if (fpsPos == std::string::npos) break;

            auto idStart = idPos + 3;
            auto idEnd = output.find_first_of(", ", idStart);
            if (idEnd == std::string::npos) break;

            std::string idStr = output.substr(idStart, idEnd - idStart);
            int id = 0;
            try {
                id = std::stoi(idStr);
            } catch (...) {
                pos = idEnd + 1;
                continue;
            }

            auto fpsStart = fpsPos + 4;
            auto fpsEnd = output.find_first_of(",}", fpsStart);
            if (fpsEnd == std::string::npos) break;

            std::string fpsStr = output.substr(fpsStart, fpsEnd - fpsStart);
            int fps = parseFps(fpsStr);

            if (id > 0 && fps > 0) {
                idToFps[fps] = id;
            }

            pos = fpsEnd + 1;
        }

        return idToFps;
    }
};

#endif