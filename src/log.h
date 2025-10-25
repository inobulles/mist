#pragma once

#include <android/log.h>

#define LOGI(...) ((void) __android_log_print(ANDROID_LOG_INFO, "mist-log", __VA_ARGS__))
#define LOGW(...) ((void) __android_log_print(ANDROID_LOG_WARN, "mist-log", __VA_ARGS__))
#define LOGE(...) ((void) __android_log_print(ANDROID_LOG_ERROR, "mist-log", __VA_ARGS__))
