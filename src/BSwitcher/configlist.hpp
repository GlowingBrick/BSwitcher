#ifndef CONFIG_LIST
#define CONFIG_LIST
/*前端的设置页面由此定义*/
#include <nlohmann/json.hpp>
const nlohmann::json CONFIG_SCHEMA = {                             //定义前端的配置页面
    {{"key", "low_battery_threshold"},                             //对应的key
     {"type", "number"},                                           //类型
     {"label", "低电量阈值"},                                      //前端显示的内容
     {"description", "电池电量低于此百分比时自动切换到熄屏模式"},  //标签
     {"min", 1},                                                   //最小
     {"max", 100},                                                 //最大
     {"category", "电源管理"}},                                    //分类

    {{"key", "enable_dynamic"},
     {"type", "checkbox"},
     {"label", "启用动态切换"},
     {"description", "核心功能，根据前台应用切换调度模式"},
     {"category", "模式设置"}},

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
     {"description", "使用inotify而非高频轮询"},
     {"category", "基本设置"}},

    {{"key", "power_monitoring"},
     {"type", "checkbox"},
     {"label", "能耗监控"},
     {"description", "记录能耗信息"},
     {"category", "电源管理"},
     {"affects", {"dual_battery"}}},

    {{"key", "dual_battery"},
     {"type", "checkbox"},
     {"label", "双电芯"},
     {"description", "双电芯设备"},
     {"category", "电源管理"},
     {"dependsOn", {{"field", "power_monitoring"}, {"condition", true}}}},

    {{"key", "clear_monitoring"},
     {"type", "button"},
     {"label", "清空能耗记录"},
     {"description", "清空现有的能耗记录"},
     {"category", "电源管理"},
     {"require_confirmation", true}},  //标记需要确认

    {{"key", "screen_off"},
     {"type", "select"},
     {"label", "熄屏模式"},
     {"description", "屏幕关闭时自动切换到的模式"},
     {"category", "模式设置"},
     {"options", "availableModes"},
     {"dependsOn", {{"field", "scene_strict"}, {"condition", false}}}},
    
    {{"key", "custom_mode"},
     {"type", "text"},
     {"label", "自定义模式"},
     {"description", "在模式列表添加一个可选的自定义模式(需调度器支持)"},
     {"category", "模式设置"}},
    
    {{"key", "dynamic_fps"},
     {"type", "checkbox"},
     {"label", "动态刷新率"},
     {"description", "启用动态刷新率"},
     {"category", "动态刷新率"},
     {"affects", {"up_fps","down_fps","fps_idle_time","screen_resolution"}}},

    {{"key", "up_fps"},
     {"type", "select"},
     {"label", "触摸刷新率"},
     {"description", "触摸时切换的默认刷新率"},
     {"category", "动态刷新率"},
     {"options", "availableFps"},
     {"dependsOn", {{"field", "dynamic_fps"}, {"condition", true}}}},

    {{"key", "down_fps"},
     {"type", "select"},
     {"label", "空闲刷新率"},
     {"description", "空闲时切换的默认刷新率"},
     {"category", "动态刷新率"},
     {"options", "availableFps"},
     {"dependsOn", {{"field", "dynamic_fps"}, {"condition", true}}}},

    {{"key", "fps_idle_time"},
     {"type", "number"},
     {"label", "空闲等待时间"},
     {"description", "进入空闲状态的时间(毫秒)"},
     {"category", "动态刷新率"},
    {"dependsOn", {{"field", "dynamic_fps"}, {"condition", true}}}},

    {{"key", "fps_backdoor"},
     {"type", "checkbox"},
     {"label", "使用Backdoor"},
     {"description", "据说更强兼容更好, 可能引发问题, 可能须重启"},
     {"category", "动态刷新率"},
    {"dependsOn", {{"field", "dynamic_fps"}, {"condition", true}}}},

    {{"key", "fps_backdoor_id"},
     {"type", "number"},
     {"label", "SERVICE CODE"},
     {"description", "Backdoor使用。指向DisplayModeRecord,一般1035"},
     {"category", "动态刷新率"},
    {"dependsOn", {{"field", "dynamic_fps"}, {"condition", true}}}}};

const nlohmann::json CONFIG_PCFG = {
    {{"key", "scene"},
     {"type", "checkbox"},
     {"label", "Scene模式"},
     {"description", "使用Scene的调度配置接口"},
     {"category", "模式设置"},
     {"affects", {"mode_file", "scene_strict"}}},  //影响其他项

    {{"key", "scene_strict"},
     {"type", "checkbox"},
     {"label", "严格Scene模式"},
     {"description", "尽力模仿Scene行为"},
     {"category", "模式设置"},
     {"dependsOn", {{"field", "scene"}, {"condition", true}}},  //条件
     {"affects", {"screen_off"}}},

    {{"key", "mode_file"},
     {"type", "text"},
     {"label", "模式文件路径"},
     {"description", "手动指定模式配置文件路径"},
     {"category", "模式设置"},
     {"dependsOn", {{"field", "scene"}, {"condition", false}}}}};

nlohmann::json CONFIG_RESO={    //大多数设备用不到这一项
    {{"key", "screen_resolution"},
     {"type", "select"},
     {"label", "屏幕分辨率"},
     {"description", "Backdoor使用。找到多个显示模式,可能需要指定。需要选定与主屏幕匹配的选项"},
     {"category", "动态刷新率"},
    {"dependsOn", {{"field", "dynamic_fps"}, {"condition", true}}}}
};

#endif