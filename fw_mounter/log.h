#ifndef LOG_H
#define LOG_H

#include <cutils/klog.h>

#define LOG(x...)   KLOG_ERROR("fw_mounter", x)

#endif
