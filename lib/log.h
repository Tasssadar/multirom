/*
 * Copyright (C) 2010 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _INIT_LOG_H_
#define _INIT_LOG_H_

#include "mrom_data.h"

#ifdef LOG_TO_STDOUT
  #include <stdio.h>
  #define ERROR(fmt, ...) fprintf(stderr, "%s: " fmt "\n", mrom_log_tag(), ##__VA_ARGS__)
  #define INFO(fmt, ...) printf("%s: " fmt "\n", mrom_log_tag(),  ##__VA_ARGS__)
#else
  #include <cutils/klog.h>

  #define ERROR(fmt, ...) klog_write(3, "<3>%s: " fmt, mrom_log_tag(), ##__VA_ARGS__)
  #define INFO(fmt, ...) klog_write(6, "<6>%s: " fmt, mrom_log_tag(), ##__VA_ARGS__)
#endif

#endif
