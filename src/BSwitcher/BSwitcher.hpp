#include "JSONSocket/JSONSocket.hpp"
#include <ForegroundApp.hpp>
#include <JSONSocketModule/ApplistModule.hpp>
#include <JSONSocketModule/ConfigModule.hpp>
#include <JSONSocketModule/InformationModule.hpp>
#include <JSONSocketModule/MonitorModule.hpp>
#include <JSONSocketModule/DynamicFps.hpp>
#include <chrono>
#include <configlist.hpp>
#include <filesystem>
#include <inotifywatcher.hpp>
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
#include <sched.h>
#include <string>
#include <thread>

#define unlikely(x) __builtin_expect(!!(x), 0)

typedef struct {
    std::string name;
    std::string author;
    std::string version;
    std::string entry;
} static_data;

class BSwitcher {
private:
    std::shared_ptr<JSONSocket> jsonSocket;

    std::shared_ptr<MainConfigTarget> mainConfigTarget;  //所有模块
    std::shared_ptr<SchedulerConfigTarget> schedulerConfigTarget;
    std::shared_ptr<InfoConfigTarget> infoConfigTarget;
    std::shared_ptr<ApplistConfigTarget> appListTarget;
    std::shared_ptr<SimpleDataTarget> configlistTarget;
    std::shared_ptr<SimpleDataTarget> availableModesTarget;
    std::shared_ptr<PowerMonitorTarget> powerMonitorTarget;
    std::shared_ptr<ConfigButtonTarget> configButtonTarget;
    std::shared_ptr<DynamicFpsTarget> dynamicFpsTarget;

    std::shared_ptr<FileWatcher> fileWatcher;  //管理inotify

    std::string sState = "";                                                  //状态文件入口
    std::string sEntry = "";                                                  //状态脚本入口，一般/data/powercfg.sh
    std::chrono::milliseconds sleepDuring = std::chrono::milliseconds(1000);  //休眠时间

    std::function<bool(const std::string&)> write_mode;  //写状态函数

    static_data _staticData;  //静态数据

    bool sceneStrict = false;     //严格scene
    std::string currentApp = "";  //前台app
    bool staticMode = false;      //静态模式

    std::string command_callback(const std::string& key);  //前端中按钮的响应

    int init_service();

    bool unscene_write_mode(const std::string& mode);  // 非scene模式写mode

    bool scene_write_mode(const std::string& mode);  // scene模式写mode

    bool dummy_write_mode(const std::string& mode);  //空的写函数，防段错误

    void init_thread();

    int load_config();  //在此加载配置

    bool ScreenState();

    int getBatteryLevel();

public:
    BSwitcher();

    BSwitcher(static_data staticData);
    bool init();
    void main_loop();
};