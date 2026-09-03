#pragma once

#include <android/log.h>

#define SPLATKIT_LOG_TAG "SplatKit"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, SPLATKIT_LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, SPLATKIT_LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, SPLATKIT_LOG_TAG, __VA_ARGS__)
