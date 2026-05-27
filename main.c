#include <stdio.h>

#include "pico/stdlib.h"

#include "FreeRTOS.h"
#include "task.h"

static TaskHandle_t heartbeat_task_handle = NULL;
static TaskHandle_t monitor_task_handle = NULL;

static void heartbeat_task(void *params) {
    (void)params;

    TickType_t last_wake_time = xTaskGetTickCount();

    while (true) {
        printf("[heartbeat] tick=%lu priority=%lu\r\n",
               (unsigned long)xTaskGetTickCount(),
               (unsigned long)uxTaskPriorityGet(NULL));

        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(1000));
    }
}

static void monitor_task(void *params) {
    (void)params;

    while (true) {
        printf("[monitor] tick=%lu heartbeat_stack=%lu monitor_stack=%lu\r\n",
               (unsigned long)xTaskGetTickCount(),
               (unsigned long)uxTaskGetStackHighWaterMark(heartbeat_task_handle),
               (unsigned long)uxTaskGetStackHighWaterMark(monitor_task_handle));

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

int main(void) {
    stdio_init_all();
    sleep_ms(2000);

    printf("Starting FreeRTOS scheduler\r\n");

    xTaskCreate(heartbeat_task, "heartbeat", 512, NULL, tskIDLE_PRIORITY + 2, &heartbeat_task_handle);
    xTaskCreate(monitor_task, "monitor", 512, NULL, tskIDLE_PRIORITY + 1, &monitor_task_handle);

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
