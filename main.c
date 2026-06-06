#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>

#include "epaper_2in15g.h"
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

typedef enum {
    DISPLAY_REQUEST_TEST_PATTERN,
    DISPLAY_REQUEST_SLEEP,
} display_request_type_t;

typedef struct {
    display_request_type_t type;
} display_request_t;

enum {
    SAMPLE_QUEUE_LENGTH = 4,
    DISPLAY_QUEUE_LENGTH = 2,
    SAMPLE_EVENT_INTERVAL = 2,
    EVENT_TASK_WORK_MS = 5000,
    SERIAL_CONNECT_TIMEOUT_MS = 15000,
    SERIAL_READY_DELAY_MS = 300,
    DEFAULT_TASK_STACK_WORDS = 512,
    UI_TASK_STACK_WORDS = 1024,
};

static QueueHandle_t sample_queue = NULL;
static QueueHandle_t display_queue = NULL;
static SemaphoreHandle_t log_mutex = NULL;
static TaskHandle_t producer_task_handle = NULL;
static TaskHandle_t consumer_task_handle = NULL;
static TaskHandle_t event_task_handle = NULL;
static TaskHandle_t ui_task_handle = NULL;
static TaskHandle_t monitor_task_handle = NULL;
static uint8_t display_frame_buffer[EPAPER_2IN15G_BUFFER_SIZE];

static void create_task_or_panic(TaskFunction_t task_code,
                                 const char *name,
                                 configSTACK_DEPTH_TYPE stack_depth,
                                 UBaseType_t priority,
                                 TaskHandle_t *task_handle) {
    const BaseType_t result = xTaskCreate(task_code, name, stack_depth, NULL, priority, task_handle);
    configASSERT(result == pdPASS);
}

static void log_printf(const char *format, ...) {
    configASSERT(log_mutex != NULL);

    if (xSemaphoreTake(log_mutex, portMAX_DELAY) == pdPASS) {
        va_list args;
        va_start(args, format);
        vprintf(format, args);
        va_end(args);
        fflush(stdout);

        xSemaphoreGive(log_mutex);
    }
}

static void set_display_pixel(uint32_t x, uint32_t y, epaper_2in15g_color_t color) {
    if ((x >= EPAPER_2IN15G_WIDTH) || (y >= EPAPER_2IN15G_HEIGHT)) {
        return;
    }

    const uint32_t stride = EPAPER_2IN15G_WIDTH / EPAPER_2IN15G_PIXELS_PER_BYTE;
    const uint32_t index = (y * stride) + (x / EPAPER_2IN15G_PIXELS_PER_BYTE);
    const uint32_t shift = (EPAPER_2IN15G_PIXELS_PER_BYTE - 1 - (x % EPAPER_2IN15G_PIXELS_PER_BYTE)) * 2;
    const uint8_t mask = (uint8_t)(0x3u << shift);

    display_frame_buffer[index] =
        (uint8_t)((display_frame_buffer[index] & ~mask) | ((uint8_t)color << shift));
}

static void fill_display(epaper_2in15g_color_t color) {
    memset(display_frame_buffer,
           epaper_2in15g_pack4(color, color, color, color),
           sizeof(display_frame_buffer));
}

static void fill_display_rect(uint32_t x0,
                              uint32_t y0,
                              uint32_t width,
                              uint32_t height,
                              epaper_2in15g_color_t color) {
    for (uint32_t y = y0; y < (y0 + height); y++) {
        for (uint32_t x = x0; x < (x0 + width); x++) {
            set_display_pixel(x, y, color);
        }
    }
}

static void draw_display_test_pattern(void) {
    fill_display(EPAPER_2IN15G_WHITE);

    const uint32_t band_height = EPAPER_2IN15G_HEIGHT / 4;
    fill_display_rect(0, 0, EPAPER_2IN15G_WIDTH, band_height, EPAPER_2IN15G_BLACK);
    fill_display_rect(0, band_height, EPAPER_2IN15G_WIDTH, band_height, EPAPER_2IN15G_WHITE);
    fill_display_rect(0, band_height * 2, EPAPER_2IN15G_WIDTH, band_height, EPAPER_2IN15G_YELLOW);
    fill_display_rect(0,
                      band_height * 3,
                      EPAPER_2IN15G_WIDTH,
                      EPAPER_2IN15G_HEIGHT - band_height * 3,
                      EPAPER_2IN15G_RED);
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

static void ui_task(void *params) {
    (void)params;

    while (true) {
        display_request_t request;

        if (xQueueReceive(display_queue, &request, portMAX_DELAY) != pdPASS) {
            continue;
        }

        switch (request.type) {
        case DISPLAY_REQUEST_TEST_PATTERN:
            log_printf("[ui] test pattern requested\r\n");
            epaper_2in15g_init_io();
            log_printf("[ui] io ready busy=%lu\r\n",
                       (unsigned long)(epaper_2in15g_busy_level() ? 1 : 0));

            if (!epaper_2in15g_init_panel()) {
                log_printf("[ui] panel init timeout busy=%lu\r\n",
                           (unsigned long)(epaper_2in15g_busy_level() ? 1 : 0));
                break;
            }

            log_printf("[ui] panel ready busy=%lu\r\n",
                       (unsigned long)(epaper_2in15g_busy_level() ? 1 : 0));
            draw_display_test_pattern();
            log_printf("[ui] display refresh start\r\n");

            if (epaper_2in15g_display(display_frame_buffer)) {
                log_printf("[ui] display refresh done busy=%lu\r\n",
                           (unsigned long)(epaper_2in15g_busy_level() ? 1 : 0));
                vTaskDelay(pdMS_TO_TICKS(10000));
                epaper_2in15g_sleep();
                log_printf("[ui] panel sleep\r\n");
            } else {
                log_printf("[ui] display timeout busy=%lu\r\n",
                           (unsigned long)(epaper_2in15g_busy_level() ? 1 : 0));
            }
            break;

        case DISPLAY_REQUEST_SLEEP:
            epaper_2in15g_sleep();
            log_printf("[ui] panel sleep requested\r\n");
            break;
        }
    }
}

static void monitor_task(void *params) {
    (void)params;

    while (true) {
        log_printf("[monitor] tick=%lu stack_p=%lu stack_c=%lu stack_e=%lu stack_ui=%lu stack_m=%lu queue=%lu/%lu display_queue=%lu/%lu notify=task-local\r\n",
                   (unsigned long)xTaskGetTickCount(),
                   (unsigned long)uxTaskGetStackHighWaterMark(producer_task_handle),
                   (unsigned long)uxTaskGetStackHighWaterMark(consumer_task_handle),
                   (unsigned long)uxTaskGetStackHighWaterMark(event_task_handle),
                   (unsigned long)uxTaskGetStackHighWaterMark(ui_task_handle),
                   (unsigned long)uxTaskGetStackHighWaterMark(monitor_task_handle),
                   (unsigned long)uxQueueMessagesWaiting(sample_queue),
                   (unsigned long)uxQueueSpacesAvailable(sample_queue),
                   (unsigned long)uxQueueMessagesWaiting(display_queue),
                   (unsigned long)uxQueueSpacesAvailable(display_queue));

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

    log_printf("Starting FreeRTOS e-paper UI task demo with mutex-protected logging\r\n");

    sample_queue = xQueueCreate(SAMPLE_QUEUE_LENGTH, sizeof(sample_message_t));
    configASSERT(sample_queue != NULL);

    display_queue = xQueueCreate(DISPLAY_QUEUE_LENGTH, sizeof(display_request_t));
    configASSERT(display_queue != NULL);

    create_task_or_panic(producer_task,
                         "producer",
                         DEFAULT_TASK_STACK_WORDS,
                         tskIDLE_PRIORITY + 2,
                         &producer_task_handle);
    create_task_or_panic(event_task,
                         "event",
                         DEFAULT_TASK_STACK_WORDS,
                         tskIDLE_PRIORITY + 1,
                         &event_task_handle);
    create_task_or_panic(ui_task,
                         "ui",
                         UI_TASK_STACK_WORDS,
                         tskIDLE_PRIORITY + 1,
                         &ui_task_handle);
    create_task_or_panic(consumer_task,
                         "consumer",
                         DEFAULT_TASK_STACK_WORDS,
                         tskIDLE_PRIORITY + 2,
                         &consumer_task_handle);
    create_task_or_panic(monitor_task,
                         "monitor",
                         DEFAULT_TASK_STACK_WORDS,
                         tskIDLE_PRIORITY + 1,
                         &monitor_task_handle);

    const display_request_t initial_display_request = {
        .type = DISPLAY_REQUEST_TEST_PATTERN,
    };
    const BaseType_t display_request_sent = xQueueSend(display_queue, &initial_display_request, 0);
    configASSERT(display_request_sent == pdPASS);

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
