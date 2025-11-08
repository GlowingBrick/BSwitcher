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
     {"dependsOn", {{"field", "scene_strict"}, {"condition", false}}}}};

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