#ifndef APP_EVENTS_H
#define APP_EVENTS_H

#include <stdbool.h>

#include "FreeRTOS.h"
#include "event_groups.h"

#define APP_EVENT_DISPLAY_BUSY    ((EventBits_t)(1u << 0))
#define APP_EVENT_CARD_DIRTY      ((EventBits_t)(1u << 1))
#define APP_EVENT_DIAG_ENABLED    ((EventBits_t)(1u << 2))

void app_events_init(void);

EventBits_t app_events_get_bits(void);
void app_events_set_bits(EventBits_t bits);
void app_events_clear_bits(EventBits_t bits);

void app_events_set_diag_enabled(bool enabled);
bool app_events_diag_enabled(void);

#endif
