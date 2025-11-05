#include <main.hpp>

std::shared_ptr<MainConfigTarget> mainConfigTarget;  //所有模块
std::shared_ptr<SchedulerConfigTarget> schedulerConfigTarget;
std::shared_ptr<InfoConfigTarget> infoConfigTarget;
std::shared_ptr<ApplistConfigTarget> appListTarget;
std::shared_ptr<ConfigListTarget> configlistTarget;
std::shared_ptr<AvailableModesTarget> availableModesTarget;
std::shared_ptr<PowerMonitorTarget> powerMonitorTarget;
std::shared_ptr<ConfigButtonTarget> configButtonTarget;

std::string sState = "";                             //状态文件入口
std::string sEntry = "";                             //状态脚本入口，一般/data/powercfg.sh
auto sleepDuring = std::chrono::microseconds(1000);  //休眠时间
bool (*write_mode)(const std::string&);              //写状态函数
bool sceneStrict = false;                            //严格scene
std::string currentApp = "";                         //前台app

void bind_to_core() {  //绑定到0 1小核
    cpu_set_t mask;
    CPU_ZERO(&mask);
    CPU_SET(0, &mask);
    CPU_SET(1, &mask);

    sched_setaffinity(0, sizeof(mask), &mask);
}

std::string command_callback(const std::string& key) {  //前端中按钮的响应
    if (key == "clear_monitoring") {
        powerMonitorTarget->clearStats();
        return "Success.";
    } else {
        return "Unknow Command";
    }
}

int init_service() {
    mainConfigTarget = std::make_shared<MainConfigTarget>();  // 配置初始化
    schedulerConfigTarget = std::make_shared<SchedulerConfigTarget>();
    infoConfigTarget = std::make_shared<InfoConfigTarget>("Custom", "unknow", "0.0.0");
    appListTarget = std::make_shared<ApplistConfigTarget>();
    availableModesTarget = std::make_shared<AvailableModesTarget>();
    powerMonitorTarget = std::make_shared<PowerMonitorTarget>(&currentApp);
    configButtonTarget = std::make_shared<ConfigButtonTarget>(command_callback);

    const nlohmann::json CONFIG_SCHEMA = {                                                               //定义前端的配置页面
                                          {{"key", "low_battery_threshold"},                             //对应的key
                                           {"type", "number"},                                           //类型
                                           {"label", "低电量阈值"},                                      //前端显示内容
                                           {"description", "电池电量低于此百分比时自动切换到省电模式"},  //标签
                                           {"min", 1},                                                   //最小
                                           {"max", 100},                                                 //最大
                                           {"category", "电源管理"}},                                    //分类

                                          {{"key", "poll_interval"},
                                           {"type", "number"},
                                           {"label", "最小轮询间隔"},
                                           {"description", "检查应用状态的最小时间间隔（秒）"},
                                           {"min", 1},
                                           {"max", 180},
                                           {"category", "基本设置"}},

                                          {{"key", "using_inotify"},
                                           {"type", "checkbox"},
                                           {"label", "使用inotify"},
                                           {"description", "使用inotify监听系统事件(重启生效)"},
                                           {"category", "基本设置"}},

                                          {{"key", "power_monitoring"},
                                           {"type", "checkbox"},
                                           {"label", "能耗监控"},
                                           {"description", "记录能耗信息"},
                                           {"category", "电源管理"}},

                                          {{"key", "clear_monitoring"},
                                           {"type", "button"},
                                           {"label", "清空能耗记录"},
                                           {"description", "清空现有的能耗记录"},
                                           {"category", "电源管理"},
                                           {"require_confirmation", true}},  //标记需要确认

                                          {{"key", "scene"},
                                           {"type", "checkbox"},
                                           {"label", "Scene模式"},
                                           {"description", "使用Scene的调度配置接口"},
                                           {"category", "模式设置"},
                                           {"affects", {"mode_file", "scene_strict"}}},  //影响其他项

                                          {{"key", "scene_strict"},
                                           {"type", "checkbox"},
                                           {"label", "严格Scene模式"},
                                           {"description", "严格模仿Scene行为"},
                                           {"category", "模式设置"},
                                           {"dependsOn", {{"field", "scene"}, {"condition", true}}},  //条件
                                           {"affects", {"screen_off"}}},

                                          {{"key", "mode_file"},
                                           {"type", "text"},
                                           {"label", "模式文件路径"},
                                           {"description", "手动指定模式配置文件路径"},
                                           {"category", "模式设置"},
                                           {"dependsOn", {{"field", "scene"}, {"condition", false}}}},

                                          {{"key", "screen_off"},
                                           {"type", "select"},
                                           {"label", "屏幕关闭模式"},
                                           {"description", "屏幕关闭时自动切换到的模式"},
                                           {"category", "模式设置"},
                                           {"options", "availableModes"},
                                           {"dependsOn", {{"field", "scene_strict"}, {"condition", false}}}}};
    configlistTarget = std::make_shared<ConfigListTarget>(CONFIG_SCHEMA);

    JSONSocket::registerConfigTarget(mainConfigTarget);  // 注册所有socket模块
    JSONSocket::registerConfigTarget(schedulerConfigTarget);
    JSONSocket::registerConfigTarget(infoConfigTarget);
    JSONSocket::registerConfigTarget(appListTarget);
    JSONSocket::registerConfigTarget(configlistTarget);
    JSONSocket::registerConfigTarget(availableModesTarget);
    JSONSocket::registerConfigTarget(powerMonitorTarget);
    JSONSocket::registerConfigTarget(configButtonTarget);

    if (!JSONSocket::initialize("/dev/BSwitcher")) {  //启用UNIX Socket
        return 0;
    }
    return 1;
}

bool unscene_write_mode(const std::string& mode) {  // 非scene模式写mode
    std::ofstream file(sState, std::ios::trunc);
    if (!file) {
        return false;  // 文件打开失败
    }

    file << mode;
    return !file.fail();
}

bool scene_write_mode(const std::string& mode) {  // scene模式写mode

    if (sceneStrict) {
        setenv("top_app", currentApp.c_str(), 1);  //模拟scene的环境变量
        setenv("scene", currentApp.c_str(), 1);
        setenv("mode", mode.c_str(), 1);
    }
    std::string command = "sh " + sEntry + " " + mode;
    int result = std::system(command.c_str());
    return result == 0;
}

bool dummy_write_mode(const std::string& mode) {  //空的写函数，防段错误
    return 1;
}

int load_config() {
    std::lock_guard<std::mutex> mLock(mainConfigTarget->configMutex);  // 获取锁

    if (mainConfigTarget->config.power_monitoring)  // 是否启用功耗监控
    {
        powerMonitorTarget->start();
    } else {
        powerMonitorTarget->stop();
    }

    if (mainConfigTarget->config.poll_interval <= 1) {  //间隔时间为1以下时
        sleepDuring = std::chrono::microseconds(100);
    } else {
        sleepDuring = std::chrono::microseconds((mainConfigTarget->config.poll_interval - 1) * 1000);  // 定义时间间隔
    }

    write_mode = dummy_write_mode;  // 防段错误
    static bool lastscene = false;  //记录scenemode是否改变
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

                    std::string sauthor = "Undefined";
                    std::string sname = "Undefined";
                    std::string sversion = "Undefined";

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
                        sceneStrict = (powercfg["features"].value("strict", false)) && mainConfigTarget->config.scene_strict;  //都为true时才启用
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
                    LOGD("Config loaded");
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
            mainConfigTarget->config.scene = false; //关闭scene模式
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
            infoConfigTarget->setData("No config available", "", "");
            LOGE("No config available, Waiting for configuration.");

            return -1;
        }
    }
    write_mode = (mainConfigTarget->config.scene) ? scene_write_mode : unscene_write_mode;  // 定义写入函数

    if (sceneStrict) {
        LOGI("Strict Scene Enabled");
    }

    return 0;
}

bool ScreenState() {
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

int getBatteryLevel() { //读取电量信息
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

int main() {

    if (fork() > 0) {
        exit(0);
    }

    setsid();

    if (fork() > 0) {
        exit(0);  // 孤儿模拟器.jpg
    }
    LOGI("BSwitcher is preparing...");

    if (!init_service()) {
        LOGE("Failed to initialize JSONSocket");
        return -1;
    }

    bind_to_core();

    std::string newMode;
    std::string lastMode = "";

    bool usinotify = true;
    {
        std::lock_guard<std::mutex> lock(mainConfigTarget->configMutex);
        usinotify = mainConfigTarget->config.using_inotify;  //提前取出inotify
    }

    if (usinotify) {
        std::vector<std::string> files_to_watch = {
            "/dev/cpuset/top-app/cgroup.procs",     // 前台变化时响应
            "/dev/cpuset/top-app/tasks",            //有时候有用
            "/dev/cpuset/restricted/cgroup.procs",  // 熄屏时响应
            "/dev/cpuset/restricted/tasks"};        //可能有用
        FileWatcher::initialize(files_to_watch);
    }

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
        if (unlikely(mainModify)) {  //检查配置文件是否修改
            if (load_config() == -1) {
                while (1) {  // 无效数据，重试
                    sleep(10);
                    if (mainModify == true) {
                        if (load_config() == 0) {
                            break;
                        }
                    }
                }
            }
            mainModify = false;
            lastMode = "";
        }

        std::this_thread::sleep_for(sleepDuring);              //等待
        FileWatcher::wait(timeset);                            // 阻塞等待cgroup变化
        std::this_thread::sleep_for(std::chrono::seconds(1));  // 等1秒防抖，避免出现none

        {
            std::unique_lock<std::mutex> mLock(mainMutex);

            timeset = 40000;
            if (!ScreenState()) {
                newMode = sceneStrict ? "standby" : mainConfig.screen_off;  //在严格的scene模式下使用standby
                timeset = 180000;                                           //降低检查频率
                currentApp = "";
                LOGD("Found screen off,Increase sleep time");

            } else if (getBatteryLevel() < mainConfig.low_battery_threshold) {  //低电量
                newMode = "powersave";

            } else {
                newMode = schedulerConfig.defaultMode;
                {
                    mLock.unlock();
                    std::lock_guard<std::mutex> sLock(schedulerMutex);

                    if (!schedulerConfig.apps.empty()) {  //为空时跳过
                        currentApp = topAppDetector.getForegroundApp();

                        LOGD("CurrentAPP: %s", currentApp.c_str());
                        if (!currentApp.empty()) {                        //未获取到时跳过
                            for (const auto& app : schedulerConfig.apps)  // 匹配应用列表
                            {                                             // 遍历app列表
                                if (app.pkgName == currentApp) {
                                    newMode = app.mode;
                                    break;
                                }
                                std::this_thread::sleep_for(std::chrono::milliseconds(1));  // 避免负载集中
                            }
                        }
                    }
                }
            }
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

    powerMonitorTarget->stop();  // 应该运行不到这
    JSONSocket::stop();
    LOGE("Main function abnormal exit");
    exit(-1);
    return 0;
}