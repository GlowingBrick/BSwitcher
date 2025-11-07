/*提供前台应用检测*/
/*三种实现，自动选择可用的版本*/

#ifndef TOP_APP_HPP
#define TOP_APP_HPP

#include "Alog.hpp"
#include <cstring>
#include <fstream>
#include <sched.h>
#include <stdio.h>
#include <chrono>
#include <thread>

class TopAppDetector {
private:
    signed char g_displayPolicyIndent = -1;
    signed char g_mTopFullscreenIndent = -1;

    using DetectorFunc = std::string (TopAppDetector::*)();
    DetectorFunc workingFunction;  //真正工作的函数

    std::string __getForegroundApp_backup();
    std::string __getForegroundApp_lru();  //lru兼容更旧的系统，但是分屏时不准
    void __initIndentationConfig();
    std::string __getForegroundApp();

    std::string __preProcessing();

public:
    TopAppDetector();

    std::string getForegroundApp();
};

#endif