#ifndef APP_CONSOLE_H
#define APP_CONSOLE_H

#include "FreeRTOS.h"
#include "task.h"

void app_console_start(configSTACK_DEPTH_TYPE stack_depth, UBaseType_t priority);
UBaseType_t app_console_stack_high_water_mark(void);

#endif
