#include "app_monitor.h"

#include "app_events.h"
#include "app_sample.h"
#include "app_system_stats.h"

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
            app_system_stats_print_summary("monitor");
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
