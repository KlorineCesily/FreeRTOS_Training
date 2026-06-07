#ifndef APP_SAMPLE_H
#define APP_SAMPLE_H

#include <stdbool.h>

#include "FreeRTOS.h"
#include "task.h"

void app_sample_start(configSTACK_DEPTH_TYPE stack_depth,
                      UBaseType_t producer_consumer_priority,
                      UBaseType_t event_priority);

void app_sample_set_detail_logs_enabled(bool enabled);
bool app_sample_detail_logs_enabled(void);

UBaseType_t app_sample_queue_messages_waiting(void);
UBaseType_t app_sample_queue_spaces_available(void);

UBaseType_t app_sample_producer_stack_high_water_mark(void);
UBaseType_t app_sample_consumer_stack_high_water_mark(void);
UBaseType_t app_sample_event_stack_high_water_mark(void);

#endif
