#include "Nebula/Core/Log.h"

#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <unistd.h>

namespace {

std::mutex g_logMutex;
FILE* g_logFile = nullptr;

const char* PriorityName(int priority) {
    switch (priority) {
        case ANDROID_LOG_DEBUG: return "D";
        case ANDROID_LOG_INFO: return "I";
        case ANDROID_LOG_WARN: return "W";
        case ANDROID_LOG_ERROR: return "E";
        case ANDROID_LOG_FATAL: return "F";
        default: return "?";
    }
}

} // namespace

namespace Nebula {

void LogPrint(int priority, const char* format, ...) {
    va_list args;
    va_start(args, format);

    va_list logcatArgs;
    va_copy(logcatArgs, args);
    __android_log_vprint(
        priority, "NebulaIL2CPP", format, logcatArgs);
    va_end(logcatArgs);

    std::lock_guard<std::mutex> lock(g_logMutex);
    if (g_logFile != nullptr) {
        timespec now{};
        clock_gettime(CLOCK_REALTIME, &now);
        tm local{};
        localtime_r(&now.tv_sec, &local);
        char timestamp[32]{};
        std::strftime(
            timestamp, sizeof(timestamp),
            "%Y-%m-%d %H:%M:%S", &local);
        std::fprintf(
            g_logFile, "%s.%03ld %s NebulaIL2CPP: ",
            timestamp, now.tv_nsec / 1000000L,
            PriorityName(priority));
        std::vfprintf(g_logFile, format, args);
        std::fputc('\n', g_logFile);
        std::fflush(g_logFile);
    }
    va_end(args);
}

bool SetLogFileDescriptor(int fileDescriptor) {
    if (fileDescriptor < 0) {
        return false;
    }
    FILE* file = fdopen(fileDescriptor, "a");
    if (file == nullptr) {
        close(fileDescriptor);
        return false;
    }
    std::lock_guard<std::mutex> lock(g_logMutex);
    if (g_logFile != nullptr) {
        std::fclose(g_logFile);
    }
    g_logFile = file;
    std::setvbuf(g_logFile, nullptr, _IOLBF, 0);
    return true;
}

void LogBuildInformation() {
    NEBULA_LOGI("NebulaIL2CPP starting (%s %s, arm64-v8a)", __DATE__, __TIME__);
}

} // namespace Nebula
