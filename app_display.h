#ifndef APP_DISPLAY_H
#define APP_DISPLAY_H

#include <stddef.h>

#include "FreeRTOS.h"

typedef void (*app_display_log_fn_t)(const char *format, ...);

void app_display_init(app_display_log_fn_t log_fn);
void app_display_task(void *params);

void app_display_request_test_pattern(void);
void app_display_request_clear_white(void);
void app_display_request_sleep(void);
void app_display_request_card(const char *command_name, size_t card_index);

UBaseType_t app_display_queue_messages_waiting(void);
UBaseType_t app_display_queue_spaces_available(void);

#endif
