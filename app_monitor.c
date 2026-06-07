#include "app_monitor.h"

#include "app_console.h"
#include "app_display.h"
#include "app_events.h"
#include "app_log.h"
#include "app_sample.h"

enum {
    MONITOR_INTERVAL_MS = 2000,
};

static TaskHandle_t monitor_task_handle = NULL;

static void create_task_or_panic(TaskFunction_t task_code,
                                 const char *name,
                                 configSTACK_DEPTH_TYPE stack_depth,
                                 UBaseType_t priority,
                                 TaskHandle_t *task_handle) {
    const BaseType_t result = xTaskCreate(task_code, name, stack_depth, NULL, priority, task_handle);
    configASSERT(result == pdPASS);
}

static void monitor_task(void *params) {
    (void)params;

    while (true) {
        (void)app_events_wait_bits(APP_EVENT_DIAG_ENABLED, pdFALSE, pdFALSE, portMAX_DELAY);

        while (app_sample_detail_logs_enabled()) {
            const EventBits_t event_bits = app_events_get_bits();

            app_log_printf("[monitor] tick=%lu events=0x%lx busy=%lu dirty=%lu diag=%lu stack_p=%lu stack_c=%lu stack_e=%lu stack_ui=%lu stack_cli=%lu stack_m=%lu queue=%lu/%lu display_queue=%lu/%lu notify=task-local\r\n",
                           (unsigned long)xTaskGetTickCount(),
                           (unsigned long)event_bits,
                           (unsigned long)((event_bits & APP_EVENT_DISPLAY_BUSY) ? 1 : 0),
                           (unsigned long)((event_bits & APP_EVENT_CARD_DIRTY) ? 1 : 0),
                           (unsigned long)((event_bits & APP_EVENT_DIAG_ENABLED) ? 1 : 0),
                           (unsigned long)app_sample_producer_stack_high_water_mark(),
                           (unsigned long)app_sample_consumer_stack_high_water_mark(),
                           (unsigned long)app_sample_event_stack_high_water_mark(),
                           (unsigned long)app_display_stack_high_water_mark(),
                           (unsigned long)app_console_stack_high_water_mark(),
                           (unsigned long)app_monitor_stack_high_water_mark(),
                           (unsigned long)app_sample_queue_messages_waiting(),
                           (unsigned long)app_sample_queue_spaces_available(),
                           (unsigned long)app_display_queue_messages_waiting(),
                           (unsigned long)app_display_queue_spaces_available());

            vTaskDelay(pdMS_TO_TICKS(MONITOR_INTERVAL_MS));
        }
    }
}

void app_monitor_start(configSTACK_DEPTH_TYPE stack_depth, UBaseType_t priority) {
    configASSERT(monitor_task_handle == NULL);

    create_task_or_panic(monitor_task,
                         "monitor",
                         stack_depth,
                         priority,
                         &monitor_task_handle);
}

UBaseType_t app_monitor_stack_high_water_mark(void) {
    configASSERT(monitor_task_handle != NULL);
    return uxTaskGetStackHighWaterMark(monitor_task_handle);
}
