#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>

#include "pico/stdlib.h"
#include "pico/stdio_usb.h"

#include "app_init.h"

#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"

typedef struct {
    uint32_t sequence;
    TickType_t produced_at;
} sample_message_t;

enum {
    SAMPLE_QUEUE_LENGTH = 4,
    SAMPLE_EVENT_INTERVAL = 2,
    EVENT_TASK_WORK_MS = 5000,
    SERIAL_CONNECT_TIMEOUT_MS = 15000,
    SERIAL_READY_DELAY_MS = 300,
};

static QueueHandle_t sample_queue = NULL;
static SemaphoreHandle_t log_mutex = NULL;
static TaskHandle_t producer_task_handle = NULL;
static TaskHandle_t consumer_task_handle = NULL;
static TaskHandle_t event_task_handle = NULL;
static TaskHandle_t monitor_task_handle = NULL;

static void create_task_or_panic(TaskFunction_t task_code,
                                 const char *name,
                                 UBaseType_t priority,
                                 TaskHandle_t *task_handle) {
    const BaseType_t result = xTaskCreate(task_code, name, 512, NULL, priority, task_handle);
    configASSERT(result == pdPASS);
}

static void log_printf(const char *format, ...) {
    configASSERT(log_mutex != NULL);

    if (xSemaphoreTake(log_mutex, portMAX_DELAY) == pdPASS) {
        va_list args;
        va_start(args, format);
        vprintf(format, args);
        va_end(args);

        xSemaphoreGive(log_mutex);
    }
}

static void producer_task(void *params) {
    (void)params;

    uint32_t sequence = 0;
    TickType_t last_wake_time = xTaskGetTickCount();

    while (true) {
        sample_message_t message = {
            .sequence = sequence++,
            .produced_at = xTaskGetTickCount(),
        };

        if (xQueueSend(sample_queue, &message, pdMS_TO_TICKS(100)) == pdPASS) {
            log_printf("[producer] seq=%lu tick=%lu\r\n",
                       (unsigned long)message.sequence,
                       (unsigned long)message.produced_at);
        } else {
            log_printf("[producer] queue full, drop seq=%lu\r\n",
                       (unsigned long)message.sequence);
        }

        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(1000));
    }
}

static void consumer_task(void *params) {
    (void)params;

    uint32_t processed_count = 0;

    while (true) {
        sample_message_t message;

        if (xQueueReceive(sample_queue, &message, portMAX_DELAY) == pdPASS) {
            const TickType_t now = xTaskGetTickCount();
            processed_count++;

            log_printf("[consumer] seq=%lu produced_at=%lu latency=%lu\r\n",
                       (unsigned long)message.sequence,
                       (unsigned long)message.produced_at,
                       (unsigned long)(now - message.produced_at));

            if ((processed_count % SAMPLE_EVENT_INTERVAL) == 0) {
                const BaseType_t result = xTaskNotifyGive(event_task_handle);
                configASSERT(result == pdPASS);

                log_printf("[consumer] notified event task at sample=%lu\r\n",
                           (unsigned long)processed_count);
            }
        }
    }
}

static void event_task(void *params) {
    (void)params;

    uint32_t handled_batches = 0;

    while (true) {
        const uint32_t pending_before_take = ulTaskNotifyTake(pdFALSE, portMAX_DELAY);

        if (pending_before_take > 0) {
            handled_batches++;
            log_printf("[event] batch=%lu samples_reported=%lu pending_before_take=%lu\r\n",
                       (unsigned long)handled_batches,
                       (unsigned long)(handled_batches * SAMPLE_EVENT_INTERVAL),
                       (unsigned long)pending_before_take);

            vTaskDelay(pdMS_TO_TICKS(EVENT_TASK_WORK_MS));
        }
    }
}

static void monitor_task(void *params) {
    (void)params;

    while (true) {
        log_printf("[monitor] tick=%lu stack_p=%lu stack_c=%lu stack_e=%lu stack_m=%lu queue=%lu/%lu notify=task-local\r\n",
                   (unsigned long)xTaskGetTickCount(),
                   (unsigned long)uxTaskGetStackHighWaterMark(producer_task_handle),
                   (unsigned long)uxTaskGetStackHighWaterMark(consumer_task_handle),
                   (unsigned long)uxTaskGetStackHighWaterMark(event_task_handle),
                   (unsigned long)uxTaskGetStackHighWaterMark(monitor_task_handle),
                   (unsigned long)uxQueueMessagesWaiting(sample_queue),
                   (unsigned long)uxQueueSpacesAvailable(sample_queue));

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
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

    log_mutex = xSemaphoreCreateMutex();
    configASSERT(log_mutex != NULL);

    log_printf("Starting FreeRTOS task notification demo with mutex-protected logging\r\n");

    sample_queue = xQueueCreate(SAMPLE_QUEUE_LENGTH, sizeof(sample_message_t));
    configASSERT(sample_queue != NULL);

    create_task_or_panic(producer_task, "producer", tskIDLE_PRIORITY + 2, &producer_task_handle);
    create_task_or_panic(event_task, "event", tskIDLE_PRIORITY + 1, &event_task_handle);
    create_task_or_panic(consumer_task, "consumer", tskIDLE_PRIORITY + 2, &consumer_task_handle);
    create_task_or_panic(monitor_task, "monitor", tskIDLE_PRIORITY + 1, &monitor_task_handle);

    vTaskDelete(NULL);
}

int main(void) {
    app_init();

    create_task_or_panic(startup_task, "startup", tskIDLE_PRIORITY + 3, NULL);

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
