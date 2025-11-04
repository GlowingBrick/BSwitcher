/*****************************************
 * This module implements foreground application detection on Android based on dumpsys.
 * Developed by BCK.
*****************************************/
#ifndef __HFAPP__
#define __HFAPP__

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