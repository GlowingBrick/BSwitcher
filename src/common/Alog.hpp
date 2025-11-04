/*提供基础的日志记录定义*/
/*支持Android logd和printf*/
#ifndef __ALOG__
#define __ALOG__

#define LOG_TAG "BSwitcher"
#include <iostream>
#if !defined(__ANDROID__) || defined(PRINTLOG)
    #include <stdio.h>
    // 非Android平台（Linux等）的日志定义
    #ifdef NDEBUG
        #define LOGV(...) ((void)0)
        #define LOGD(...) ((void)0)
        #define LOGI(fmt, ...) printf("[I/" LOG_TAG "] " fmt "\n", ##__VA_ARGS__)
        #define LOGW(fmt, ...) printf("[W/" LOG_TAG "] " fmt "\n", ##__VA_ARGS__)
        #define LOGE(fmt, ...) printf("[E/" LOG_TAG "] " fmt "\n", ##__VA_ARGS__)
    #else
        #define LOGV(fmt, ...) printf("[V/" LOG_TAG "] " fmt "\n", ##__VA_ARGS__)
        #define LOGD(fmt, ...) printf("[D/" LOG_TAG "] " fmt "\n", ##__VA_ARGS__)
        #define LOGI(fmt, ...) printf("[I/" LOG_TAG "] " fmt "\n", ##__VA_ARGS__)
        #define LOGW(fmt, ...) printf("[W/" LOG_TAG "] " fmt "\n", ##__VA_ARGS__)
        #define LOGE(fmt, ...) printf("[E/" LOG_TAG "] " fmt "\n", ##__VA_ARGS__)
    #endif
#else
    #include <android/log.h>
    // Android 平台的日志定义
    #ifdef NDEBUG
        #define LOGV(...) ((void)0)
        #define LOGD(...) ((void)0)
        #define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
        #define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
        #define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
    #else
        #define LOGV(...) __android_log_print(ANDROID_LOG_VERBOSE, LOG_TAG, __VA_ARGS__)
        #define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
        #define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
        #define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
        #define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
    #endif
#endif

#endif