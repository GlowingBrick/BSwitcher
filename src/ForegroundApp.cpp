/*****************************************
 * This module implements foreground application detection on Android based on dumpsys.
 * Developed by BCK with assistance from DeepSeek.
*****************************************/
#include<ForegroundApp.hpp>
/*  好用但可能不可靠的神奇手动算法
signed char g_displayPolicyIndent = -1;
signed char g_mTopFullscreenIndent = -1;

int countLeadingSpaces(const std::string& line) {
    int count = 0;
    for (char c : line) {
        if (c == ' ' || c == '\t') count++;
        else break;
    }
    return count;
}

void initIndentationConfig() {  //启动时预先统计标签前置空格，以防不同系统差异
    
    char buffer[256];

    FILE* pipe = popen("dumpsys activity activities | grep -E 'mTopFullscreen'", "r");  
    fgets(buffer, sizeof(buffer), pipe);
    g_mTopFullscreenIndent=countLeadingSpaces(buffer);
    pclose(pipe);

    pipe=popen("dumpsys activity activities | grep -E 'DisplayPolicy'", "r");
    fgets(buffer, sizeof(buffer), pipe);
    g_displayPolicyIndent=countLeadingSpaces(buffer);
    pclose(pipe);
    
    // 设置默认值（如果未检测到）
    if (g_displayPolicyIndent < 0) g_displayPolicyIndent = 2;
    if (g_mTopFullscreenIndent < 0) g_mTopFullscreenIndent = 4;
    
    LOGI("Detected indentation: DisplayPolicy=%d, mTopFullscreen=%d", 
         g_displayPolicyIndent, g_mTopFullscreenIndent);
}


std::string getForegroundApp() {
    if(unlikely(g_displayPolicyIndent<0)){
        initIndentationConfig();
    }
    FILE* pipe = popen("dumpsys activity activities", "r");
    if (!pipe) {
        LOGE("Failed to execute dumpsys command");
        return "";
    }

    char buffer[256];
    std::string result;
    bool foundDisplayPolicy = false;

    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        if (!foundDisplayPolicy) {  //首先寻找DisplayPolicy，因为D开头的行更少
            if (buffer[g_displayPolicyIndent] == 'D') {   //快速检索指定位置为D的行
                if(buffer[g_displayPolicyIndent+7] == 'P')  //存在不止一个Display*的标签，快速筛选P
                {
                    if (memcmp(buffer + g_displayPolicyIndent, "DisplayPolicy", 12) == 0) 
                    {
                        foundDisplayPolicy=true;
                    }
                }
            }
        }
        else    //mTopFullscreen只出现在DisplayPolicy下，随后逐行寻找
        {
            if (buffer[g_mTopFullscreenIndent+1] == 'T')    //快速检索指定位置为T的行
            {
                if (memcmp(buffer + g_mTopFullscreenIndent, "mTopFullscreen", 14) == 0) //精确匹配验证
                {
                    std::string line(buffer);
                    size_t slashPos = line.find('/');
                    if (slashPos != std::string::npos && slashPos > 0) { // 提取包名
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
    
    if (!result.empty()) {
    } else {
        LOGW("[Foreground App Detection] No foreground app detected (Raw output: %s)", buffer);
    }
    
    return result;
}
*/



std::string getForegroundApp() {
    std::string packageName;
    LOGD("Getting ForegroundApp");
    FILE* pipe = popen("dumpsys activity activities | grep '^[[:space:]]*mTopFullscreen'", "r");
    if (!pipe) {
        throw std::runtime_error("popen() failed!");
    }
    
    char buffer[256];
    packageName="none";
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