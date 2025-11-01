/*****************************************
 * This module implements foreground application detection on Android based on dumpsys.
 * Developed by BCK.
*****************************************/
#ifndef __HFAPP__
#define __HFAPP__

#include <cstring>
#include <stdio.h>
#include <sched.h>
#include <fstream>
#include "Alog.hpp"

extern signed char g_displayPolicyIndent;
extern signed char g_mTopFullscreenIndent;

#define unlikely(x) __builtin_expect(!!(x), 0)


std::string getForegroundApp();

#endif