/*提供前台应用检测*/
/*三种实现，自动选择可用的版本*/

#include <ForegroundApp.hpp>
#include <cerrno>

std::string TopAppDetector::__getForegroundApp_backup() {  //使用grep的备用方案
    std::string packageName;
    LOGD("Getting ForegroundApp");
    FILE* pipe = popen("dumpsys activity activities | grep '^[[:space:]]*mTopFullscreen'", "r");
    if (!pipe) {
        //不可抛异常：调用方(BSwitcher::main_loop)没有捕获，会导致worker进程abort。
        //返回空串即可，__preProcessing 与主循环都按"未获取到"处理。
        LOGE("popen() failed: %s", strerror(errno));
        return "";
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

//探测单个标签的前置空格数，失败返回-1
static int __probeIndent(const char* command) {
    FILE* pipe = popen(command, "r");
    if (!pipe) {  //原代码未检查，pipe为NULL时fgets直接段错误
        LOGE("popen() failed: %s", strerror(errno));
        return -1;
    }

    char buffer[256] = {0};  //必须初始化：fgets失败时不写入buffer，
                             //未初始化的栈数据既无NUL终止符，也会被误当成有效行解析
    bool hasLine = (fgets(buffer, sizeof(buffer), pipe) != nullptr);
    pclose(pipe);

    if (!hasLine) {  // grep无匹配输出
        return -1;
    }
    return __countLeadingSpaces(buffer);
}

void TopAppDetector::__initIndentationConfig() {  //启动时预先统计标签前置空格，以防不同系统差异

    g_mTopFullscreenIndent = __probeIndent("dumpsys activity activities | grep -E 'mTopFullscreen'");

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    g_displayPolicyIndent = __probeIndent("dumpsys activity activities | grep -E 'DisplayPolicy'");

    LOGD("Detected indentation: DisplayPolicy=%d, mTopFullscreen=%d",
         g_displayPolicyIndent, g_mTopFullscreenIndent);
}

std::string TopAppDetector::__getForegroundApp() {  //手动筛选，理论上能跑
    if (g_displayPolicyIndent < 0 || g_mTopFullscreenIndent < 0) {
        //缩进未探测成功时不能按偏移访问：负值转size_t后会回绕，导致越界读
        return "";
    }

    FILE* pipe = popen("dumpsys activity activities", "r");
    if (!pipe) {
        LOGE("Failed to execute dumpsys command");
        return "";
    }

    char buffer[256];
    std::string result;
    bool foundDisplayPolicy = false;

    const size_t policyIndent = static_cast<size_t>(g_displayPolicyIndent);
    const size_t topIndent = static_cast<size_t>(g_mTopFullscreenIndent);

    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        //按缩进定位前先确认行足够长，否则会读到行尾之外的未初始化数据。
        //真正的标签行长度必然大于缩进+标签长度，故不会漏掉有效行。
        const size_t len = strlen(buffer);

        if (!foundDisplayPolicy) {                                 //首先寻找DisplayPolicy，因为D开头的行更少
            if (len > policyIndent + 13) {                         //"DisplayPolicy"共13字符
                if (buffer[policyIndent] == 'D') {                 //快速检索指定位置为D的行
                    if (buffer[policyIndent + 7] == 'P')           //存在不止一个Display*的标签，快速筛选P
                    {
                        if (memcmp(buffer + policyIndent, "DisplayPolicy", 12) == 0) {
                            foundDisplayPolicy = true;
                        }
                    }
                }
            }
        } else  //mTopFullscreen只出现在DisplayPolicy下，随后逐行寻找
        {
            if (len > topIndent + 14)                       //"mTopFullscreen"共14字符
            {
                if (buffer[topIndent + 1] == 'T')  //快速检索指定位置为T的行
                {
                    if (memcmp(buffer + topIndent, "mTopFullscreen", 14) == 0)  //精确匹配验证
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
    }

    pclose(pipe);

    return result;
}

std::string TopAppDetector::__getForegroundApp_lru() {  //lru兼容更旧的系统，但是分屏时不准
    FILE* pipe = popen("dumpsys activity lru", "r");
    if (!pipe) return "";

    char buffer[256];
    std::string result="";

    while (fgets(buffer, sizeof(buffer), pipe)) {
        if (result.empty()) {
            size_t len = strlen(buffer);
            size_t startPos = 0;
            size_t endPos = 0;

            for (size_t i = 16; i < len; ++i) {
                if (buffer[i] == ':') {
                    startPos = i + 1;                       //包名起始点
                } else if (buffer[i] == '/' && startPos) {  //包名结束
                    endPos = i;
                    break;
                }
            }

            //确保是TOP
            if (startPos && endPos && endPos > startPos) {
                bool foundValidTOP = false;
                for (int i = startPos - 4; i >= 0; --i) {
                    if (i + 3 < static_cast<int>(startPos) &&
                        buffer[i] == 'T' &&
                        buffer[i + 1] == 'O' &&
                        buffer[i + 2] == 'P') {
                        if (i == 0 || buffer[i - 1] != 'B') {
                            foundValidTOP = true;
                            break;
                        }
                    }
                }

                if (foundValidTOP) {
                    result.assign(buffer + startPos, endPos - startPos);
                    break;
                }
            }
        }
    }
    pclose(pipe);
    return result;
}

std::string TopAppDetector::__preProcessing() { //在这里测试几个不同方法，然后调整指针固化
    static int j = 0;  //记录dumpsys activity activities失败的次数

    std::string a = __getForegroundApp_backup();
    if (!a.empty()) {  //检查dumpsys activity activities是否可用
        j = -1;
    } else {
        if (j >= 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            std::string c = __getForegroundApp_lru();
            if (!c.empty()) {
                ++j;
            }
            if (j >= 5) {
                LOGD("activities unavailable,Using lru instead.");
                workingFunction = &TopAppDetector::__getForegroundApp_lru;
            }
            return c;
        }
    }

    if (g_displayPolicyIndent < 0 || g_mTopFullscreenIndent < 0) {
        __initIndentationConfig();
    }

    if (g_displayPolicyIndent < 0 || g_mTopFullscreenIndent < 0) {
        return a;
    }

    static int i = 0;  //记录手动检索失败的次数
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    std::string b = __getForegroundApp();  
    if (!a.empty() && b.empty()) {  //检查dumpsys activity activities的手动版本是否可用
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
