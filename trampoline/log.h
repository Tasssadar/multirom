#ifndef LOG_H
#define LOG_H

#include <cutils/klog.h>

#define ERROR(x...)   KLOG_ERROR("trampoline", x)
#define NOTICE(x...)  KLOG_NOTICE("trampoline", x)
#define INFO(x...)    KLOG_INFO("trampoline", x)

#endif