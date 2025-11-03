/*****************************************
 * This module implements foreground application detection on Android based on dumpsys.
 * Developed by BCK with assistance from DeepSeek.
*****************************************/
#include <ForegroundApp.hpp>

std::string TopAppDetector::__getForegroundApp_backup() {  //备用方案
    std::string packageName;
    LOGD("Getting ForegroundApp");
    FILE* pipe = popen("dumpsys activity activities | grep '^[[:space:]]*mTopFullscreen'", "r");
    if (!pipe) {
        throw std::runtime_error("popen() failed!");
    }

    char buffer[256];
    packageName = "";
    if (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        std::string line(buffer);

        size_t slashPos = line.find('/');
        if (slashPos != std::string::npos && slashPos > 0) {
            size_t startPos = slashPos - 1;

            while (startPos > 0 && line[startPos] != ' ') {
                startPos--;
            }
            if (line[startPos] == ' ') {
                startPos++;
            }

            if (startPos < slashPos) {
                packageName = line.substr(startPos, slashPos - startPos);
            }
        }
    }

    pclose(pipe);
    return packageName;
}

int __countLeadingSpaces(const std::string& line) {
    if (line.empty()) {
        return -1;
    }
    int count = 0;
    for (char c : line) {
        if (c == ' ' || c == '\t')
            count++;
        else
            break;
    }
    return count;
}

void TopAppDetector::__initIndentationConfig() {  //启动时预先统计标签前置空格，以防不同系统差异

    char buffer[256];

    FILE* pipe = popen("dumpsys activity activities | grep -E 'mTopFullscreen'", "r");
    fgets(buffer, sizeof(buffer), pipe);

    g_mTopFullscreenIndent = __countLeadingSpaces(buffer);
    pclose(pipe);

    pipe = popen("dumpsys activity activities | grep -E 'DisplayPolicy'", "r");
    fgets(buffer, sizeof(buffer), pipe);
    g_displayPolicyIndent = __countLeadingSpaces(buffer);
    pclose(pipe);

    // 设置默认值（如果未检测到）
    if (g_displayPolicyIndent < 0) g_displayPolicyIndent = 2;
    if (g_mTopFullscreenIndent < 0) g_mTopFullscreenIndent = 4;

    LOGD("Detected indentation: DisplayPolicy=%d, mTopFullscreen=%d",
         g_displayPolicyIndent, g_mTopFullscreenIndent);
}

std::string TopAppDetector::__getForegroundApp() {  //手动筛选，理论上能跑
    FILE* pipe = popen("dumpsys activity activities", "r");
    if (!pipe) {
        LOGE("Failed to execute dumpsys command");
        return "";
    }

    char buffer[256];
    std::string result;
    bool foundDisplayPolicy = false;

    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        if (!foundDisplayPolicy) {                             //首先寻找DisplayPolicy，因为D开头的行更少
            if (buffer[g_displayPolicyIndent] == 'D') {        //快速检索指定位置为D的行
                if (buffer[g_displayPolicyIndent + 7] == 'P')  //存在不止一个Display*的标签，快速筛选P
                {
                    if (memcmp(buffer + g_displayPolicyIndent, "DisplayPolicy", 12) == 0) {
                        foundDisplayPolicy = true;
                    }
                }
            }
        } else  //mTopFullscreen只出现在DisplayPolicy下，随后逐行寻找
        {
            if (buffer[g_mTopFullscreenIndent + 1] == 'T')  //快速检索指定位置为T的行
            {
                if (memcmp(buffer + g_mTopFullscreenIndent, "mTopFullscreen", 14) == 0)  //精确匹配验证
                {
                    std::string line(buffer);
                    size_t slashPos = line.find('/');
                    if (slashPos != std::string::npos && slashPos > 0) {  // 提取包名
                        size_t startPos = slashPos - 1;
                        while (startPos > 0 && line[startPos] != ' ') {
                            startPos--;
                        }

                        if (line[startPos] == ' ') {
                            startPos++;
                        }

                        if (startPos < slashPos) {
                            result = line.substr(startPos, slashPos - startPos);
                        }
                    }
                }
            }
        }
    }

    pclose(pipe);

    return result;
}

std::string TopAppDetector::__preProcessing() {
    if (g_displayPolicyIndent < 0 || g_mTopFullscreenIndent < 0) {
        __initIndentationConfig();
    }
    if (g_displayPolicyIndent < 0 || g_mTopFullscreenIndent < 0) {
        return __getForegroundApp_backup();
    }

    static int i = 0;  //记录失败的次数

    std::string a = __getForegroundApp_backup();
    std::string b = __getForegroundApp();
    if (!a.empty() && b.empty()) {
        ++i;
    }

    if (!b.empty()) {
        LOGD("Fast search available.");
        workingFunction = &TopAppDetector::__getForegroundApp;
    }

    if (i >= 5) {
        LOGD("Fast search unavailable. Using grep instead.");
        workingFunction = &TopAppDetector::__getForegroundApp_backup;
    }

    return a;
}

TopAppDetector::TopAppDetector() {
    workingFunction = &TopAppDetector::__preProcessing;
    g_displayPolicyIndent = -1;
    g_mTopFullscreenIndent = -1;
}

std::string TopAppDetector::getForegroundApp() {
    return (this->*workingFunction)();
}
