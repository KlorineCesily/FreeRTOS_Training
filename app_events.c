#include "app_events.h"

static EventGroupHandle_t system_events = NULL;

void app_events_init(void) {
    configASSERT(system_events == NULL);

    system_events = xEventGroupCreate();
    configASSERT(system_events != NULL);
}

EventBits_t app_events_get_bits(void) {
    configASSERT(system_events != NULL);
    return xEventGroupGetBits(system_events);
}

void app_events_set_bits(EventBits_t bits) {
    configASSERT(system_events != NULL);
    (void)xEventGroupSetBits(system_events, bits);
}

void app_events_clear_bits(EventBits_t bits) {
    configASSERT(system_events != NULL);
    (void)xEventGroupClearBits(system_events, bits);
}

void app_events_set_diag_enabled(bool enabled) {
    if (enabled) {
        app_events_set_bits(APP_EVENT_DIAG_ENABLED);
    } else {
        app_events_clear_bits(APP_EVENT_DIAG_ENABLED);
    }
}

bool app_events_diag_enabled(void) {
    return (app_events_get_bits() & APP_EVENT_DIAG_ENABLED) != 0;
}
