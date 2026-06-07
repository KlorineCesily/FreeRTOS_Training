#include <stdbool.h>

#include "pico/stdio_usb.h"
#include "pico/stdlib.h"

#include "app_console.h"
#include "app_display.h"
#include "app_init.h"
#include "app_log.h"
#include "app_monitor.h"
#include "app_sample.h"

#include "FreeRTOS.h"
#include "task.h"

enum {
    SERIAL_CONNECT_TIMEOUT_MS = 15000,
    SERIAL_READY_DELAY_MS = 300,
    DEFAULT_TASK_STACK_WORDS = 512,
    UI_TASK_STACK_WORDS = 1024,
};

static void create_task_or_panic(TaskFunction_t task_code,
                                 const char *name,
                                 configSTACK_DEPTH_TYPE stack_depth,
                                 UBaseType_t priority,
                                 TaskHandle_t *task_handle) {
    const BaseType_t result = xTaskCreate(task_code, name, stack_depth, NULL, priority, task_handle);
    configASSERT(result == pdPASS);
}

static void wait_for_serial_monitor(void) {
    const TickType_t start = xTaskGetTickCount();
    const TickType_t timeout = pdMS_TO_TICKS(SERIAL_CONNECT_TIMEOUT_MS);

    while (!stdio_usb_connected() && (xTaskGetTickCount() - start) < timeout) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    vTaskDelay(pdMS_TO_TICKS(SERIAL_READY_DELAY_MS));
}

static void startup_task(void *params) {
    (void)params;

    wait_for_serial_monitor();

    app_log_init();
    app_log_printf("Starting FreeRTOS e-paper UI task demo with mutex-protected logging\r\n");

    app_sample_start(DEFAULT_TASK_STACK_WORDS,
                     tskIDLE_PRIORITY + 2,
                     tskIDLE_PRIORITY + 1);

    app_display_init(app_log_printf);
    app_display_start(UI_TASK_STACK_WORDS, tskIDLE_PRIORITY + 1);

    app_console_start(DEFAULT_TASK_STACK_WORDS, tskIDLE_PRIORITY + 1);
    app_monitor_start(DEFAULT_TASK_STACK_WORDS, tskIDLE_PRIORITY + 1);

    app_display_request_test_pattern();

    vTaskDelete(NULL);
}

int main(void) {
    app_init();

    create_task_or_panic(startup_task,
                         "startup",
                         DEFAULT_TASK_STACK_WORDS,
                         tskIDLE_PRIORITY + 3,
                         NULL);

    vTaskStartScheduler();

    while (true) {
        tight_loop_contents();
    }
}

void vApplicationMallocFailedHook(void) {
    taskDISABLE_INTERRUPTS();

    while (true) {
        tight_loop_contents();
    }
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name) {
    (void)task;
    (void)task_name;

    taskDISABLE_INTERRUPTS();

    while (true) {
        tight_loop_contents();
    }
}
