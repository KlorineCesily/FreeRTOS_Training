#include "app_sample.h"

#include <stdint.h>

#include "app_log.h"

#include "queue.h"

typedef struct {
    uint32_t sequence;
    TickType_t produced_at;
} sample_message_t;

enum {
    SAMPLE_QUEUE_LENGTH = 4,
    SAMPLE_EVENT_INTERVAL = 2,
    EVENT_TASK_WORK_MS = 5000,
};

static QueueHandle_t sample_queue = NULL;
static volatile bool detail_logs_enabled = false;
static TaskHandle_t producer_task_handle = NULL;
static TaskHandle_t consumer_task_handle = NULL;
static TaskHandle_t event_task_handle = NULL;

static void create_task_or_panic(TaskFunction_t task_code,
                                 const char *name,
                                 configSTACK_DEPTH_TYPE stack_depth,
                                 UBaseType_t priority,
                                 TaskHandle_t *task_handle) {
    const BaseType_t result = xTaskCreate(task_code, name, stack_depth, NULL, priority, task_handle);
    configASSERT(result == pdPASS);
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
            if (detail_logs_enabled) {
                app_log_printf("[producer] seq=%lu tick=%lu\r\n",
                               (unsigned long)message.sequence,
                               (unsigned long)message.produced_at);
            }
        } else {
            app_log_printf("[producer] queue full, drop seq=%lu\r\n",
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

            if (detail_logs_enabled) {
                app_log_printf("[consumer] seq=%lu produced_at=%lu latency=%lu\r\n",
                               (unsigned long)message.sequence,
                               (unsigned long)message.produced_at,
                               (unsigned long)(now - message.produced_at));
            }

            if ((processed_count % SAMPLE_EVENT_INTERVAL) == 0) {
                const BaseType_t result = xTaskNotifyGive(event_task_handle);
                configASSERT(result == pdPASS);

                if (detail_logs_enabled) {
                    app_log_printf("[consumer] notified event task at sample=%lu\r\n",
                                   (unsigned long)processed_count);
                }
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
            if (detail_logs_enabled) {
                app_log_printf("[event] batch=%lu samples_reported=%lu pending_before_take=%lu\r\n",
                               (unsigned long)handled_batches,
                               (unsigned long)(handled_batches * SAMPLE_EVENT_INTERVAL),
                               (unsigned long)pending_before_take);
            }

            vTaskDelay(pdMS_TO_TICKS(EVENT_TASK_WORK_MS));
        }
    }
}

void app_sample_start(configSTACK_DEPTH_TYPE stack_depth,
                      UBaseType_t producer_consumer_priority,
                      UBaseType_t event_priority) {
    configASSERT(sample_queue == NULL);

    sample_queue = xQueueCreate(SAMPLE_QUEUE_LENGTH, sizeof(sample_message_t));
    configASSERT(sample_queue != NULL);

    create_task_or_panic(producer_task,
                         "producer",
                         stack_depth,
                         producer_consumer_priority,
                         &producer_task_handle);
    create_task_or_panic(event_task,
                         "event",
                         stack_depth,
                         event_priority,
                         &event_task_handle);
    create_task_or_panic(consumer_task,
                         "consumer",
                         stack_depth,
                         producer_consumer_priority,
                         &consumer_task_handle);
}

void app_sample_set_detail_logs_enabled(bool enabled) {
    detail_logs_enabled = enabled;
}

bool app_sample_detail_logs_enabled(void) {
    return detail_logs_enabled;
}

UBaseType_t app_sample_queue_messages_waiting(void) {
    configASSERT(sample_queue != NULL);
    return uxQueueMessagesWaiting(sample_queue);
}

UBaseType_t app_sample_queue_spaces_available(void) {
    configASSERT(sample_queue != NULL);
    return uxQueueSpacesAvailable(sample_queue);
}

UBaseType_t app_sample_producer_stack_high_water_mark(void) {
    configASSERT(producer_task_handle != NULL);
    return uxTaskGetStackHighWaterMark(producer_task_handle);
}

UBaseType_t app_sample_consumer_stack_high_water_mark(void) {
    configASSERT(consumer_task_handle != NULL);
    return uxTaskGetStackHighWaterMark(consumer_task_handle);
}

UBaseType_t app_sample_event_stack_high_water_mark(void) {
    configASSERT(event_task_handle != NULL);
    return uxTaskGetStackHighWaterMark(event_task_handle);
}
