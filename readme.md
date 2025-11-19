## **BSwitcher**

Android的一个轻量化的、无需保活的调度器管理器模块

使用webui进行配置。magisk用户可能需要安装ksuwebui

## **配置文件**

主程序默认在二进制文件所在位置读取、建立配置文件。通过-p参数可指定其他路径

### static_data.json 
**static_data.json不会被程序主动创建。它存在且可用时，程序将不再尝试/data/powercfg.json，也不再接受调度器接口文件的配置，而是使用本文件的内容覆盖**

如果希望将此程序集成在其他工程中，可以考虑定义此文件以固化配置

本文件不允许缺任何项，出现缺失将会放弃加载。

- name: 显示的调度名称
- author: 显示的调度作者
- version: 显示的调度版本
- entry: 调度的配置文件。即mode改变时，会被写入此文件。

示例（此处为对接CuprumTurbo-Scheduler的模块版本）
```json
{
    "name":"CuprumTurbo-Scheduler",
    "author" : "chenzyadb",
    "version":"8.3.10",
    "entry": "/storage/emulated/0/Android/ct/cur_mode.txt"
}
```

### config.json
此文件会主动创建。但运行中很少再加载
- poll_interval: 轮询最小间隔。在非轮询模式下，这标记相邻两次触发的最小间隔
- low_battery_threshold: 电量低于此值时，将触发切换省电模式与低刷新率
- enable_dynamic: 是否启用动态切换。为false时不会将不再实现动态切换
- scene: 使用scene定义的接口，即/data/powercfg.json          
- scene_strict: 尝试模仿scene严格模式的部分行为，包括环境变量等
- mode_file: 在不使用scene模式下，可用手动指定模式的写入文件     
- screen_off: 熄屏时切换的模式,scene_strict为true时锁定standby   
- using_inotify: 尝试监听cgroup以捕捉前台切换信号，而非轮询    
- power_monitoring: 启用能耗监控。仅在亮屏非充电情况下运行   
- dual_battery: 双电芯，即能耗x2
- custom_mode: 在模式列表中添加一个可选的自定义模式选项       
- dynamic_fps: 启用动态刷新率         
- fps_idle_time: 等待此毫秒空闲后切换到down_fps
- down_fps: 空闲刷新率                   
- up_fps: 触摸时刷新率                
- fps_backdoor: 使用sf的backdoor调节。可能引发系统的其他问题。
- fps_backdoor_id: backdoor的操作id,一般1035
- screen_resolution: backdoor启用时，存在多个分辨率的设备可能需要指定，以免调整中混乱


### scheduler_config.json
此文件会主动创建。但运行中很少再加载
- defaultMode: 默认的模式
- rules: 应用规则
    - appPackage: 包名
    - mode: 模式
    - up_fps: 同上，覆盖全局规则
    - down_fps: 同上，覆盖全局规则



## **接口**
Webui通过Unix Socket与服务对接。默认位置/dev/BSwitcher。可以手动访问

通信内容为JSON

字段内容: 
- target: 标识访问的目标
- mode: read或write
- data: write时可用，包含数据内容。

通信后服务将返回结果并主动断开连接。

示例 (实际使用中不允许注释)
```json
{
    "target":"config",  //访问主配置
    "mode":"write", //写入主配置
    "data":{
        "poll_interval":2   //修改轮询间隔为2
    }
}
```

当前可用target有: 
- config: 主配置
- scheduler: 应用规则配置,rules必须完整传入
- info: 调度信息，只读
- configlist: Webui的设置项列表，只读
- availableModes: 可用模式列表，只读
- applist: 所有应用列表，只读
- powerdata: 功耗记录信息，只读
- dynamicFps: 可用刷新率信息，只读

*由于较旧的Android不支持`nc -U`，所以在此类设备中将有一个socket_send工具 (tool/dontHaveNc.cpp)被挂载到system/bin，以为Webui提供通信支持*

## **编译**: 
需要ndk,并定义ANDROID_NDK环境变量
```bash
git clone --recurse-submodules https://github.com/GlowingBrick/BSwitcher.git
cd BSwitcher
mkdir build
cd build
cmake ..
make -j4
```

## **引用依赖**
BSwitcher：
- [nlohmann json](https://github.com/nlohmann/json) - MIT License

Webui:
- [React](https://reactjs.org/) - MIT License
- [Vite](https://vitejs.dev/) - MIT License

    及他们的依赖。详见webui/package.json

此外很大程度上参考
- [Dfps](https://github.com/yc9559/dfps)

