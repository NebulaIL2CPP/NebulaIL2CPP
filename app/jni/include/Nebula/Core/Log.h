#pragma once

#include <android/log.h>

#define NEBULA_LOGD(...) \
    Nebula::LogPrint(ANDROID_LOG_DEBUG, __VA_ARGS__)
#define NEBULA_LOGI(...) \
    Nebula::LogPrint(ANDROID_LOG_INFO, __VA_ARGS__)
#define NEBULA_LOGW(...) \
    Nebula::LogPrint(ANDROID_LOG_WARN, __VA_ARGS__)
#define NEBULA_LOGE(...) \
    Nebula::LogPrint(ANDROID_LOG_ERROR, __VA_ARGS__)

namespace Nebula {
void LogPrint(int priority, const char* format, ...)
    __attribute__((format(printf, 2, 3)));
bool SetLogFileDescriptor(int fileDescriptor);
void LogBuildInformation();
}
