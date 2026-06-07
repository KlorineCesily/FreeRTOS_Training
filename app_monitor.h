#ifndef APP_MONITOR_H
#define APP_MONITOR_H

#include "FreeRTOS.h"
#include "task.h"

void app_monitor_start(configSTACK_DEPTH_TYPE stack_depth, UBaseType_t priority);
UBaseType_t app_monitor_stack_high_water_mark(void);

#endif
