#include <main.hpp>

std::shared_ptr<MainConfigTarget> mainConfigTarget;
std::shared_ptr<SchedulerConfigTarget> schedulerConfigTarget;
std::shared_ptr<InfoConfigTarget> infoConfigTarget;
std::shared_ptr<ApplistConfigTarget> appListTarget;
std::shared_ptr<ConfigListTarget> configlistTarget;
std::shared_ptr<AvailableModesTarget> availableModesTarget;
std::shared_ptr<PowerMonitorTarget> powerMonitorTarget;

std::string sState = "";
std::string sEntry = "";
int sleepDuring = 2;
bool (*write_mode)(const std::string &);
std::string currentApp;
int init_service()
{
    mainConfigTarget = std::make_shared<MainConfigTarget>(); // 配置初始化
    schedulerConfigTarget = std::make_shared<SchedulerConfigTarget>();
    infoConfigTarget = std::make_shared<InfoConfigTarget>("Custom", "unknow", "0.0.0");
    appListTarget = std::make_shared<ApplistConfigTarget>();
    availableModesTarget = std::make_shared<AvailableModesTarget>();
    powerMonitorTarget = std::make_shared<PowerMonitorTarget>(&currentApp);

    const nlohmann::json CONFIG_SCHEMA = {// 这是可配置接口
                                          {{"key", "low_battery_threshold"},
                                           {"type", "number"},
                                           {"label", "低电量阈值"},
                                           {"description", "电池电量低于此百分比时自动切换到省电模式"},
                                           {"min", 1},
                                           {"max", 100},
                                           {"category", "电源管理"}},
                                          {{"key", "poll_interval"},
                                           {"type", "number"},
                                           {"label", "最小轮询间隔"},
                                           {"description", "检查应用状态的最小时间间隔（秒）"},
                                           {"min", 1},
                                           {"max", 60},
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
                                          {{"key", "scene"},
                                           {"type", "checkbox"},
                                           {"label", "Scene模式"},
                                           {"description", "使用Scene的调度配置接口"},
                                           {"category", "模式设置"},
                                           {"affects", {"mode_file"}}},
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
                                           {"options", "availableModes"}}};
    configlistTarget = std::make_shared<ConfigListTarget>(CONFIG_SCHEMA);

    JSONSocket::registerConfigTarget(mainConfigTarget); // 注册所有socket模块
    JSONSocket::registerConfigTarget(schedulerConfigTarget);
    JSONSocket::registerConfigTarget(infoConfigTarget);
    JSONSocket::registerConfigTarget(appListTarget);
    JSONSocket::registerConfigTarget(configlistTarget);
    JSONSocket::registerConfigTarget(availableModesTarget);
    JSONSocket::registerConfigTarget(powerMonitorTarget);

    if (!JSONSocket::initialize("/dev/BSwitcher"))
    {
        return 0;
    }
    return 1;
}

std::string read_current_mode(const std::string &entryFile)
{
    std::ifstream file(entryFile);
    if (!file.is_open())
    {
        return ""; // 文件不存在或无法打开
    }

    std::string buffer;
    file >> buffer;

    return buffer;
}

bool unscene_write_mode(const std::string &mode)
{ // 非scene模式写mode
    std::ofstream file(sState, std::ios::trunc);
    if (!file)
    {
        return false; // 文件打开失败
    }

    file << mode;
    return !file.fail();
}

bool scene_write_mode(const std::string &mode)
{ // scene模式写mode
    std::string command = "sh " + sEntry + " " + mode;
    int result = std::system(command.c_str());
    return result == 0;
}

bool dummy_write_mode(const std::string &mode)
{
    return 1;
}

int load_config()
{
    std::unique_lock<std::mutex> lock(mainConfigTarget->configMutex);   //获取锁

    if(mainConfigTarget->config.power_monitoring)   //是否启用功耗监控
    {
        powerMonitorTarget->start();
    }
    else
    {
        powerMonitorTarget->stop();
    }

    if (mainConfigTarget->config.poll_interval <= 1)
    {
        sleepDuring = -1;
    }
    else
    {
        sleepDuring = mainConfigTarget->config.poll_interval - 1; // 定义时间间隔
    }


    write_mode = dummy_write_mode;              // 防段错误
    sleepDuring = 2;
    if (mainConfigTarget->config.scene == true) // scene模式启用时，加载/data/powercfg.json
    {
        std::ifstream powercfgfile("/data/powercfg.json");
        if (powercfgfile.is_open())
        { // 加载powercfg
            try
            {
                nlohmann::json powercfg;
                powercfgfile >> powercfg;

                std::string sname = "Unknow";
                if (powercfg.contains("name"))
                { // 解析name
                    sname = powercfg["name"];
                }

                std::string sauthor = "Unknow";
                if (powercfg.contains("author"))
                { // 解析author
                    sauthor = powercfg["author"];
                }

                std::string sversion = "Unknow";
                if (powercfg.contains("version"))
                { // 解析version
                    sversion = powercfg["version"];
                }

                if (powercfg.contains("entry"))
                { // 检查是否存在entry字段
                    sEntry = powercfg["entry"];
                }
                else
                {
                    if (std::filesystem::exists("/data/powercfg.sh"))
                    { // 否则检查是否存在/data/powercfg.sh
                        sEntry = "/data/powercfg.sh";
                    }
                    else
                    {
                        LOGE("Entry not found. Scene mode has been disabled.");
                        mainConfigTarget->config.scene = false;
                        sname = "Custom";
                        sauthor = "Unknow";
                        sversion = "Unknow";
                    }
                }

                infoConfigTarget->setData(sname, sauthor, sversion); // 装载进配置
                LOGD("Config loaded");
            }
            catch (const nlohmann::json::exception &e)
            {
                LOGE("Configuration source (powercfg.json) parsing failed. Scene mode has been disabled.");
            }
        }
        else
        { // /data/powercfg.json不存在
            LOGE("Configuration source (powercfg.json) not found. Scene mode has been disabled.");
            mainConfigTarget->config.scene = false;
        }
        powercfgfile.close();
    }

    if (mainConfigTarget->config.scene != true)
    { // 如果不是scene模式
        if (std::filesystem::exists(mainConfigTarget->config.mode_file))
        { // 如果自定义mode_file可用
            sState = mainConfigTarget->config.mode_file;
            infoConfigTarget->setData("Custom", "", "");
        }
        else
        {
            infoConfigTarget->setData("No config available", "", "");
            LOGE("No config available, Waiting for configuration.");

            return -1;
        }
    }
    write_mode = (mainConfigTarget->config.scene) ? scene_write_mode : unscene_write_mode; // 定义写入函数

    return 0;
}

bool ScreenState()
{
    // 检查背光亮度
    return powerMonitorTarget->screenstatus();;
}

int getBatteryLevel() {
    static int battery_fd = []() {
        int fd = open("/sys/class/power_supply/battery/capacity", O_RDONLY | O_CLOEXEC);
        return (fd >= 0) ? fd : -1;
    }();
    
    if (battery_fd < 0) {
        return 100;  
    }
    
    char buf[5] = {0};
    ssize_t bytes_read = pread(battery_fd, buf, sizeof(buf)-1, 0);
    
    if (bytes_read <= 0) {
        return 100; 
    }
    
    buf[bytes_read] = '\0';

    int btl=atoi(buf);
    LOGD("BatteryLevel: %d",btl);
    return btl;
}

void bind_to_core()
{
    cpu_set_t mask;
    CPU_ZERO(&mask);
    CPU_SET(0, &mask);
    CPU_SET(1, &mask);

    sched_setaffinity(0, sizeof(mask), &mask);
}

int main()
{

    if (fork() > 0)
    {
        exit(0);
    }

    setsid();

    if (fork() > 0)
    {
        exit(0); // 孤儿模拟器.jpg
    }

    if (!init_service())
    {
        LOGE("Failed to initialize JSONSocket");
        return -1;
    }

    bind_to_core();



    std::string lastMode = read_current_mode(sState);
    std::string newMode;

    bool usinotify = true;
    {
        std::unique_lock<std::mutex> lock(mainConfigTarget->configMutex);
        usinotify=mainConfigTarget->config.using_inotify;
    }

    if(usinotify)
    {
        std::vector<std::string> files_to_watch = {
            "/dev/cpuset/top-app/cgroup.procs",     //前台变化时响应
            "/dev/cpuset/top-app/tasks",
            "/dev/cpuset/restricted/cgroup.procs",  //熄屏时响应
            "/dev/cpuset/restricted/tasks"
        };
        FileWatcher::initialize(files_to_watch);
    }


    // 预先提取对象，提升效率
    auto &mainModify = mainConfigTarget->modify;
    auto &mainConfig = mainConfigTarget->config;
    auto &mainMutex = mainConfigTarget->configMutex;

    auto &schedulerMutex = schedulerConfigTarget->configMutex;
    auto &schedulerConfig = schedulerConfigTarget->config;

    int timeset=10000;

    LOGD("Enter main loop");
    while (1) // 主循环
    {
        if (unlikely(mainModify))
        {
            if (load_config() == -1)
            {
                while (1)
                { // 无效数据，重试
                    sleep(10);
                    if (mainModify == true)
                    {
                        if (load_config() == 0)
                        {
                            lastMode = read_current_mode(sState);
                            break;
                        }
                    }
                }
            }
            mainModify = false;
        }
        if (sleepDuring > 0)
        {
            sleep(sleepDuring);
        }
        else
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        FileWatcher::wait(timeset); // 阻塞等待cgroup变化
        sleep(1);            // 等1秒防抖，避免出现none

        {
            std::unique_lock<std::mutex> lock(mainMutex);

            timeset=40000;
            if (!ScreenState())
            {
                newMode = mainConfig.screen_off;
                timeset=120000;
                currentApp="";
                LOGD("Found screen off,Increase sleep time");
            }
            else if (getBatteryLevel() < mainConfig.low_battery_threshold)
            {
                newMode = "powersave";
            }
            else
            {
                newMode = schedulerConfig.defaultMode;
                {
                    std::unique_lock<std::mutex> lock(schedulerMutex);
                    if (!schedulerConfig.apps.empty())
                    {
                        currentApp = getForegroundApp();

                        LOGD("CurrentAPP: %s", currentApp.c_str());
                        if (!currentApp.empty())
                        { 
                            for (const auto &app : schedulerConfig.apps)    //匹配应用列表
                            { // 遍历app列表
                                if (app.pkgName == currentApp)
                                {
                                    newMode = app.mode;
                                    break;
                                }
                                std::this_thread::sleep_for(std::chrono::milliseconds(1)); // 避免负载集中
                            }
                        }
                    }
                }
            }
        }

        if (lastMode != newMode)    //有变化时再写入
        {
            write_mode(newMode);
            lastMode = newMode;
            LOGI("Updated to: %s", newMode.c_str());
        }
    }

    powerMonitorTarget->stop(); // 应该运行不到这
    JSONSocket::stop(); 
    LOGE("Main function abnormal exit");
    exit(-1);
    return 0;
}