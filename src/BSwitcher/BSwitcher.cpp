#include "BSwitcher.hpp"
#include <configlist.hpp>

std::string BSwitcher::command_callback(const std::string& key) {  //前端中按钮的响应
    if (key == "clear_monitoring") {
        powerMonitorTarget->clearStats();
        return "Success.";
    } else {
        return "Unknow Command";
    }
}

int BSwitcher::init_service() {
    jsonSocket = std::make_shared<JSONSocket>("/dev/BSwitcher");
    mainConfigTarget = std::make_shared<MainConfigTarget>();  // 配置初始化
    schedulerConfigTarget = std::make_shared<SchedulerConfigTarget>();
    appListTarget = std::make_shared<ApplistConfigTarget>();
    availableModesTarget = std::make_shared<SimpleDataTarget>("availableModes", nlohmann::json::array({"powersave", "balance", "performance", "fast"}));
    powerMonitorTarget = std::make_shared<PowerMonitorTarget>(&currentApp, &mainConfigTarget->config.dual_battery);
    dynamicFpsTarget = std::make_shared<DynamicFpsTarget>();
    configlistTarget = std::make_shared<SimpleDataTarget>("configlist", CONFIG_SCHEMA);
    configButtonTarget = std::make_shared<ConfigButtonTarget>(
        [this](const std::string& key) {
            return command_callback(key);
        });

    auto combined_config = CONFIG_SCHEMA;

    if (staticMode) {
        sState = _staticData.entry;
        infoConfigTarget = std::make_shared<InfoConfigTarget>(_staticData.name, _staticData.author, _staticData.version);
        mainConfigTarget->config.scene = false;
        write_mode = std::bind(&BSwitcher::unscene_write_mode, this, std::placeholders::_1);
    } else {
        infoConfigTarget = std::make_shared<InfoConfigTarget>("Custom", "unknow", "0.0.0");
        combined_config.insert(combined_config.end(), CONFIG_PCFG.begin(), CONFIG_PCFG.end());  //合并配置
    }

    if (dynamicFpsTarget->allfpsmap.size() <= 1) {
        if (dynamicFpsTarget->allfpsmap.size() == 0) {
            mainConfigTarget->config.dynamic_fps = false;
        } else {
            mainConfigTarget->config.screen_resolution = dynamicFpsTarget->allfpsmap.begin()->first;  //唯一的分辨率
        }
    } else {  //有不止一个分辨率
        nlohmann::json resolist = nlohmann::json::array();
        for (const auto& [key, value] : dynamicFpsTarget->allfpsmap) {
            resolist.push_back(nlohmann::json({{"value", key}, {"label", key}}));
            LOGD("Discovery resolution: %s", key.c_str());
        }
        CONFIG_RESO[0]["options"] = resolist;
        combined_config.insert(combined_config.end(), CONFIG_RESO.begin(), CONFIG_RESO.end());  //添加选项

        if (!dynamicFpsTarget->allfpsmap.count(mainConfigTarget->config.screen_resolution)) {  //当前分辨率不存在
            mainConfigTarget->config.screen_resolution = dynamicFpsTarget->findMax();          //装进配置
        }
    }

    configlistTarget->reLoad(combined_config);  //装载设置列表

    jsonSocket->registerConfigTarget(mainConfigTarget);  // 注册所有模块
    jsonSocket->registerConfigTarget(schedulerConfigTarget);
    jsonSocket->registerConfigTarget(infoConfigTarget);
    jsonSocket->registerConfigTarget(appListTarget);
    jsonSocket->registerConfigTarget(configlistTarget);
    jsonSocket->registerConfigTarget(availableModesTarget);
    jsonSocket->registerConfigTarget(powerMonitorTarget);
    jsonSocket->registerConfigTarget(configButtonTarget);
    jsonSocket->registerConfigTarget(dynamicFpsTarget);
    if (!jsonSocket->initialize()) {  //启动UNIX Socket
        return 0;
    }

    const std::vector<std::string> inotifyFiles = {
        "/dev/cpuset/top-app/cgroup.procs",     // 前台变化时响应
        "/dev/cpuset/top-app/tasks",            //有时候有用
        "/dev/cpuset/restricted/cgroup.procs",  // 熄屏时响应
        "/dev/cpuset/restricted/tasks"};        //可能有用

    fileWatcher = std::make_shared<FileWatcher>(inotifyFiles);

    return 1;
}

bool BSwitcher::unscene_write_mode(const std::string& mode) {  // 非scene模式写mode
    std::ofstream file(sState, std::ios::trunc);
    if (!file) {
        return false;  // 文件打开失败
    }

    file << mode;
    return !file.fail();
}

bool BSwitcher::scene_write_mode(const std::string& mode) {  // scene模式写mode

    if (sceneStrict) {
        setenv("top_app", currentApp.c_str(), 1);  //模拟scene的环境变量
        setenv("scene", currentApp.c_str(), 1);
        setenv("mode", mode.c_str(), 1);
    }
    std::string command = "sh " + sEntry + " " + mode;
    int result = std::system(command.c_str());
    return result == 0;
}

bool BSwitcher::dummy_write_mode(const std::string& mode) {  //空的写函数，不实际操作
    return 1;
}

void BSwitcher::init_thread() {
    if (mainConfigTarget->config.power_monitoring)  // 功耗监控
    {
        powerMonitorTarget->start();
    } else {
        powerMonitorTarget->stop();
    }

    if (mainConfigTarget->config.dynamic_fps) {  //动态刷新率
        //传入配置
        if (dynamicFpsTarget->allfpsmap.size() != 0) {
            auto it = dynamicFpsTarget->allfpsmap.find(mainConfigTarget->config.screen_resolution);
            if (it != dynamicFpsTarget->allfpsmap.end()) {
                dynamicFpsTarget->fpsmap.store(&it->second, std::memory_order_relaxed);
            }

            dynamicFpsTarget->using_backdoor.store(mainConfigTarget->config.fps_backdoor, std::memory_order_relaxed);
            dynamicFpsTarget->backdoorid.store(mainConfigTarget->config.fps_backdoor_id, std::memory_order_relaxed);
            dynamicFpsTarget->down_during_ms.store(mainConfigTarget->config.fps_idle_time, std::memory_order_relaxed);

            //启动
            dynamicFpsTarget->init();
        }

    } else {
        dynamicFpsTarget->stop();
    }
}

int BSwitcher::load_config() {  //在此加载配置
    if (!mainConfigTarget->modify) {
        return 1;
    }

    std::lock_guard<std::mutex> mLock(mainConfigTarget->configMutex);  // 获取锁
    mainConfigTarget->modify = false;

    if (mainConfigTarget->config.poll_interval <= 1) {  //间隔时间为1以下时
        sleepDuring = std::chrono::milliseconds(100);
    } else {
        sleepDuring = std::chrono::milliseconds((mainConfigTarget->config.poll_interval - 1) * 1000);  // 定义时间间隔
    }

    if (mainConfigTarget->config.using_inotify) {
        fileWatcher->initialize();
    } else {
        fileWatcher->cleanup();
    }

    if (!mainConfigTarget->config.custom_mode.empty()) {
        availableModesTarget->reLoad(nlohmann::json::array({"powersave", "balance", "performance", "fast", mainConfigTarget->config.custom_mode}));
    } else {
        availableModesTarget->reLoad(nlohmann::json::array({"powersave", "balance", "performance", "fast"}));
    }

    init_thread();

    if (!staticMode) {
        write_mode = std::bind(&BSwitcher::dummy_write_mode, this, std::placeholders::_1);  // 防段错误
        static bool lastscene = false;                                                      //记录scenemode是否改变
        sceneStrict = false;

        if (mainConfigTarget->config.scene == true)  // scene模式启用时，加载/data/powercfg.json
        {
            std::ifstream powercfgfile("/data/powercfg.json");
            bool shexist = std::filesystem::exists("/data/powercfg.sh");
            sEntry = "";

            if (powercfgfile.is_open() || shexist)  // 如果powercfg.json或powercfg.sh任一可用
            {                                       // 加载
                if (shexist) {
                    sEntry = "/data/powercfg.sh";  // 定义默认位置
                    infoConfigTarget->setData("Unknow Name", "", "");
                }
                if (powercfgfile.is_open()) {
                    try {
                        nlohmann::json powercfg;
                        powercfgfile >> powercfg;

                        std::string sauthor = "Unknow Name";
                        std::string sname = "Unknow";
                        std::string sversion = "Unknow";

                        if (powercfg.contains("entry")) {  // 检查是否存在entry字段
                            sEntry = powercfg["entry"];
                        } else {
                            if (std::filesystem::exists("/data/powercfg.sh")) {  // 否则检查是否存在/data/powercfg.sh
                                sEntry = "/data/powercfg.sh";
                            } else {
                                LOGE("Entry not found. Scene mode has been disabled.");
                                mainConfigTarget->config.scene = false;
                                sname = "Custom";
                                sauthor = "Unknow";
                                sversion = "Unknow";
                            }
                        }

                        if (powercfg.contains("features") && powercfg["features"].is_object()) {
                            sceneStrict = mainConfigTarget->config.scene_strict;  //严格scene模式
                        }

                        if (powercfg.contains("name")) {  // 解析name
                            sname = powercfg["name"];
                        }

                        if (powercfg.contains("author")) {  // 解析author
                            sauthor = powercfg["author"];
                        }

                        if (powercfg.contains("version")) {  // 解析version
                            sversion = powercfg["version"];
                        }

                        infoConfigTarget->setData(sname, sauthor, sversion);  // 装载进配置

                    } catch (const nlohmann::json::exception& e) {
                        LOGE("Configuration source (powercfg.json) parsing failed.");
                    }

                    if (lastscene == false) {  //避免多次init
                        scene_write_mode("init");
                    }
                }
            } else  // 都不存在
            {
                LOGE("Configuration source (powercfg.json) not found. Scene mode has been disabled.");
                mainConfigTarget->config.scene = false;  //关闭scene模式
            }
            powercfgfile.close();
        }

        if (!sceneStrict) {  //清理环境
            unsetenv("top_app");
            unsetenv("scene");
            unsetenv("mode");
        }

        lastscene = mainConfigTarget->config.scene;

        if (mainConfigTarget->config.scene != true) {  // 如果不是scene模式
            sceneStrict = false;
            if (std::filesystem::exists(mainConfigTarget->config.mode_file)) {  // 如果自定义mode_file可用
                sState = mainConfigTarget->config.mode_file;
                infoConfigTarget->setData("Custom", "", "");
            } else {  //没有可用的配置
                infoConfigTarget->setData("未对接调度", "", "");
                LOGE("No config available, Waiting for configuration.");
                sState = "/dev/null";
                return -1;
            }
        }

        if (mainConfigTarget->config.scene) {
            write_mode = std::bind(&BSwitcher::scene_write_mode, this, std::placeholders::_1);
        } else {
            write_mode = std::bind(&BSwitcher::unscene_write_mode, this, std::placeholders::_1);
        }

        if (!mainConfigTarget->config.enable_dynamic) {
            write_mode = std::bind(&BSwitcher::dummy_write_mode, this, std::placeholders::_1);  //未启用动态切换时不实际操作
        }

        if (sceneStrict) {
            LOGI("Strict Scene Enabled");
        }
    }
    LOGD("Config loaded");
    return 0;
}

bool BSwitcher::ScreenState() {
    static int screen_fd_ = []() {
        int fd = open("/dev/cpuset/restricted/cgroup.procs", O_RDONLY | O_CLOEXEC);
        return (fd >= 0) ? fd : -1;
    }();
    char buffer[128];
    int line_count = 0;
    ssize_t bytes_read;

    if ((bytes_read = pread(screen_fd_, buffer, sizeof(buffer), 0)) > 0) {
        for (ssize_t i = 0; i < bytes_read; i++) {
            if (buffer[i] == '\n') {
                line_count++;
                if (line_count >= 5) {  // /dev/cpuset/restricted/cgroup.procs大于5条时即可认为熄屏
                    powerMonitorTarget->setScreenStatus(false);
                    return false;
                }
            }
        }
    }

    powerMonitorTarget->setScreenStatus(true);
    return true;  //少于5条或不可用
}

int BSwitcher::getBatteryLevel() {  //读取电量信息
    static int battery_fd = []() {
        int fd = open("/sys/class/power_supply/battery/capacity", O_RDONLY | O_CLOEXEC);
        return (fd >= 0) ? fd : -1;
    }();

    if (battery_fd < 0) {
        return 100;
    }

    char buf[5] = {0};
    ssize_t bytes_read = pread(battery_fd, buf, sizeof(buf) - 1, 0);

    if (bytes_read <= 0) {
        return 100;
    }

    buf[bytes_read] = '\0';

    int btl = atoi(buf);
    LOGD("BatteryLevel: %d", btl);
    return btl;
}

BSwitcher::BSwitcher() {
    staticMode = false;
}

BSwitcher::BSwitcher(static_data staticData) {  //允许使用固定的调度器信息
    staticMode = true;
    _staticData = staticData;
}

bool BSwitcher::init() {
    return init_service();
}

void BSwitcher::main_loop() {
    std::string newMode;
    std::string lastMode = "";

    auto& mainModify = mainConfigTarget->modify;
    auto& mainConfig = mainConfigTarget->config;
    auto& mainMutex = mainConfigTarget->configMutex;

    auto& schedulerMutex = schedulerConfigTarget->configMutex;
    auto& schedulerConfig = schedulerConfigTarget->config;

    int timeset = 10000;

    TopAppDetector topAppDetector;

    LOGD("Ready, entering main loop.");
    while (1)  // 主循环
    {
        load_config();  //加载配置

        std::this_thread::sleep_for(sleepDuring);              //等待
        fileWatcher->wait(timeset);                            // 阻塞等待cgroup变化
        std::this_thread::sleep_for(std::chrono::seconds(1));  // 等1秒防抖，避免出现none

        {
            std::unique_lock<std::mutex> mLock(mainMutex);
            if (!(mainConfig.dynamic_fps || mainConfig.power_monitoring || mainConfig.enable_dynamic)) {
                continue;
            }
            int ufps = mainConfig.up_fps > 0 ? mainConfig.up_fps : 120;
            int dfps = mainConfig.down_fps > 0 ? mainConfig.down_fps : 60;

            timeset = 40000;
            if (!ScreenState()) {
                newMode = sceneStrict ? "standby" : mainConfig.screen_off;  //在严格的scene模式下使用standby
                timeset = 180000;                                           //降低检查频率
                LOGD("Found screen off,Increase sleep time");

            } else if (getBatteryLevel() < mainConfig.low_battery_threshold) {  //低电量
                newMode = "powersave";
                ufps = 60;
                dfps = 60;
            } else {
                newMode = schedulerConfig.defaultMode;
                {
                    mLock.unlock();
                    std::lock_guard<std::mutex> sLock(schedulerMutex);

                    currentApp = topAppDetector.getForegroundApp();
                    LOGD("CurrentAPP: %s", currentApp.c_str());

                    if (!currentApp.empty()) {                        //未获取到时跳过
                        for (const auto& app : schedulerConfig.apps)  // 匹配应用列表
                        {                                             // 遍历app列表
                            if (app.pkgName == currentApp) {
                                newMode = app.mode;
                                if (app.down_fps > 0) {
                                    dfps = app.down_fps;
                                }
                                if (app.up_fps > 0) {
                                    ufps = app.up_fps;
                                }
                                break;
                            }
                            std::this_thread::sleep_for(std::chrono::milliseconds(1));  // 避免负载集中
                        }
                    }
                }
            }
            dynamicFpsTarget->up_fps.store(ufps, std::memory_order_relaxed);
            dynamicFpsTarget->down_fps.store(dfps, std::memory_order_relaxed);
        }
        if (sceneStrict) {  //严格scene时
            static std::string lastapp = "";
            if (currentApp != lastapp) {
                write_mode(newMode);
                lastapp = currentApp;
                LOGI("Updated to: %s", newMode.c_str());
            }
        } else if (lastMode != newMode)  // 有变化时
        {
            write_mode(newMode);
            lastMode = newMode;
            LOGI("Updated to: %s", newMode.c_str());
        }
    }
}
