#ifndef LOG_H
#define LOG_H

#include <cutils/klog.h>

#define ERROR(x...)   KLOG_ERROR("fw_mounter", x)
#define NOTICE(x...)  KLOG_NOTICE("fw_mounter", x)
#define INFO(x...)    KLOG_INFO("fw_mounter", x)

#endif
