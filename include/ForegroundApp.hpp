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

class TopAppDetector {
private:
    signed char g_displayPolicyIndent = -1;
    signed char g_mTopFullscreenIndent = -1;

    using DetectorFunc = std::string (TopAppDetector::*)();
    DetectorFunc workingFunction;  //真正工作的函数

    std::string __getForegroundApp_backup();
    void __initIndentationConfig();
    std::string __getForegroundApp();

    std::string __preProcessing();

public:
    TopAppDetector();

    std::string getForegroundApp();
};

#endif