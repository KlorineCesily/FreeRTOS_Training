#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>

#include "epaper_2in15g.h"
#include "pico/error.h"
#include "pico/stdio.h"
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
    DISPLAY_REQUEST_CLEAR_WHITE,
    DISPLAY_REQUEST_SLEEP,
    DISPLAY_REQUEST_CARD_SHOW,
} display_request_type_t;

typedef struct {
    display_request_type_t type;
    size_t card_index;
} display_request_t;

typedef struct {
    const char *word;
    const char *phonetic;
    const char *meaning;
    const char *example;
    const char *note;
    epaper_2in15g_color_t accent_color;
} vocabulary_card_t;

enum {
    SAMPLE_QUEUE_LENGTH = 4,
    DISPLAY_QUEUE_LENGTH = 1,
    SAMPLE_EVENT_INTERVAL = 2,
    EVENT_TASK_WORK_MS = 5000,
    CARD_DISPLAY_WIDTH = EPAPER_2IN15G_HEIGHT,
    CARD_DISPLAY_HEIGHT = EPAPER_2IN15G_WIDTH,
    CONSOLE_LINE_LENGTH = 64,
    CONSOLE_POLL_MS = 20,
    SERIAL_CONNECT_TIMEOUT_MS = 15000,
    SERIAL_READY_DELAY_MS = 300,
    DEFAULT_TASK_STACK_WORDS = 512,
    UI_TASK_STACK_WORDS = 1024,
};

static QueueHandle_t sample_queue = NULL;
static QueueHandle_t display_queue = NULL;
static SemaphoreHandle_t log_mutex = NULL;
static volatile bool detail_logs_enabled = false;
static TaskHandle_t producer_task_handle = NULL;
static TaskHandle_t consumer_task_handle = NULL;
static TaskHandle_t event_task_handle = NULL;
static TaskHandle_t ui_task_handle = NULL;
static TaskHandle_t console_task_handle = NULL;
static TaskHandle_t monitor_task_handle = NULL;
static uint8_t display_frame_buffer[EPAPER_2IN15G_BUFFER_SIZE];
static const vocabulary_card_t vocabulary_cards[] = {
    {
        .word = "aberration",
        .phonetic = "/ae-buh-RAY-shun/",
        .meaning = "a departure from what is normal",
        .example = "The sudden result was an aberration, not a trend.",
        .note = "ab- away + errare wander",
        .accent_color = EPAPER_2IN15G_RED,
    },
    {
        .word = "mitigate",
        .phonetic = "/MIT-uh-gayt/",
        .meaning = "to make something less severe",
        .example = "Good planning can mitigate the risk.",
        .note = "exam sense: reduce harm or severity",
        .accent_color = EPAPER_2IN15G_YELLOW,
    },
    {
        .word = "resilient",
        .phonetic = "/ri-ZIL-yuhnt/",
        .meaning = "able to recover quickly",
        .example = "A resilient system keeps working after failure.",
        .note = "re- back + salire leap",
        .accent_color = EPAPER_2IN15G_BLACK,
    },
};

static size_t vocabulary_card_count(void) {
    return sizeof(vocabulary_cards) / sizeof(vocabulary_cards[0]);
}

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

static void set_landscape_display_pixel(uint32_t x, uint32_t y, epaper_2in15g_color_t color) {
    if ((x >= CARD_DISPLAY_WIDTH) || (y >= CARD_DISPLAY_HEIGHT)) {
        return;
    }

    set_display_pixel(y, EPAPER_2IN15G_HEIGHT - 1 - x, color);
}

static void fill_landscape_display_rect(uint32_t x0,
                                        uint32_t y0,
                                        uint32_t width,
                                        uint32_t height,
                                        epaper_2in15g_color_t color) {
    for (uint32_t y = y0; y < (y0 + height); y++) {
        for (uint32_t x = x0; x < (x0 + width); x++) {
            set_landscape_display_pixel(x, y, color);
        }
    }
}

static void draw_landscape_display_rect_outline(uint32_t x0,
                                                uint32_t y0,
                                                uint32_t width,
                                                uint32_t height,
                                                uint32_t thickness,
                                                epaper_2in15g_color_t color) {
    fill_landscape_display_rect(x0, y0, width, thickness, color);
    fill_landscape_display_rect(x0, y0 + height - thickness, width, thickness, color);
    fill_landscape_display_rect(x0, y0, thickness, height, color);
    fill_landscape_display_rect(x0 + width - thickness, y0, thickness, height, color);
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

static void draw_vocabulary_card_placeholder(size_t card_index) {
    const vocabulary_card_t *card = &vocabulary_cards[card_index];

    fill_display(EPAPER_2IN15G_WHITE);

    fill_landscape_display_rect(0, 0, 10, CARD_DISPLAY_HEIGHT, card->accent_color);
    fill_landscape_display_rect(10, 0, CARD_DISPLAY_WIDTH - 10, 30, EPAPER_2IN15G_BLACK);
    fill_landscape_display_rect(22, 46, CARD_DISPLAY_WIDTH - 44, 22, EPAPER_2IN15G_YELLOW);
    draw_landscape_display_rect_outline(22, 82, 126, 50, 2, EPAPER_2IN15G_BLACK);
    draw_landscape_display_rect_outline(166, 82, 108, 50, 2, EPAPER_2IN15G_RED);

    const uint32_t marker_width = 20;
    const uint32_t marker_gap = 10;
    const uint32_t marker_y = CARD_DISPLAY_HEIGHT - 18;

    for (size_t index = 0; index < vocabulary_card_count(); index++) {
        const uint32_t marker_x = 24 + (uint32_t)index * (marker_width + marker_gap);
        const epaper_2in15g_color_t color =
            index == card_index ? card->accent_color : EPAPER_2IN15G_BLACK;
        fill_landscape_display_rect(marker_x, marker_y, marker_width, 8, color);
    }
}

static const char *display_request_name(display_request_type_t type) {
    switch (type) {
    case DISPLAY_REQUEST_TEST_PATTERN:
        return "test";
    case DISPLAY_REQUEST_CLEAR_WHITE:
        return "clear";
    case DISPLAY_REQUEST_SLEEP:
        return "sleep";
    case DISPLAY_REQUEST_CARD_SHOW:
        return "card show";
    }

    return "unknown";
}

static void send_display_request(display_request_type_t type) {
    const display_request_t request = {
        .type = type,
        .card_index = 0,
    };

    const BaseType_t result = xQueueOverwrite(display_queue, &request);
    configASSERT(result == pdPASS);

    log_printf("[console] display %s request accepted (latest wins)\r\n",
               display_request_name(type));
}

static void send_card_display_request(const char *command_name, size_t card_index) {
    const display_request_t request = {
        .type = DISPLAY_REQUEST_CARD_SHOW,
        .card_index = card_index,
    };

    const BaseType_t result = xQueueOverwrite(display_queue, &request);
    configASSERT(result == pdPASS);

    log_printf("[console] %s request accepted card=%lu/%lu (latest wins)\r\n",
               command_name,
               (unsigned long)(card_index + 1),
               (unsigned long)vocabulary_card_count());
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
                log_printf("[producer] seq=%lu tick=%lu\r\n",
                           (unsigned long)message.sequence,
                           (unsigned long)message.produced_at);
            }
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

            if (detail_logs_enabled) {
                log_printf("[consumer] seq=%lu produced_at=%lu latency=%lu\r\n",
                           (unsigned long)message.sequence,
                           (unsigned long)message.produced_at,
                           (unsigned long)(now - message.produced_at));
            }

            if ((processed_count % SAMPLE_EVENT_INTERVAL) == 0) {
                const BaseType_t result = xTaskNotifyGive(event_task_handle);
                configASSERT(result == pdPASS);

                if (detail_logs_enabled) {
                    log_printf("[consumer] notified event task at sample=%lu\r\n",
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
                log_printf("[event] batch=%lu samples_reported=%lu pending_before_take=%lu\r\n",
                           (unsigned long)handled_batches,
                           (unsigned long)(handled_batches * SAMPLE_EVENT_INTERVAL),
                           (unsigned long)pending_before_take);
            }

            vTaskDelay(pdMS_TO_TICKS(EVENT_TASK_WORK_MS));
        }
    }
}

static bool prepare_display_panel(void) {
    epaper_2in15g_init_io();
    log_printf("[ui] io ready busy=%lu\r\n",
               (unsigned long)(epaper_2in15g_busy_level() ? 1 : 0));

    if (!epaper_2in15g_init_panel()) {
        log_printf("[ui] panel init timeout busy=%lu\r\n",
                   (unsigned long)(epaper_2in15g_busy_level() ? 1 : 0));
        return false;
    }

    log_printf("[ui] panel ready busy=%lu\r\n",
               (unsigned long)(epaper_2in15g_busy_level() ? 1 : 0));
    return true;
}

static void refresh_vocabulary_card(size_t card_index, bool *panel_awake) {
    const vocabulary_card_t *card = &vocabulary_cards[card_index];

    log_printf("[ui] card %lu/%lu word=%s %s\r\n",
               (unsigned long)(card_index + 1),
               (unsigned long)vocabulary_card_count(),
               card->word,
               card->phonetic);
    log_printf("[ui] meaning=%s\r\n", card->meaning);
    log_printf("[ui] note=%s\r\n", card->note);
    log_printf("[ui] example=%s\r\n", card->example);

    if (!prepare_display_panel()) {
        return;
    }

    *panel_awake = true;
    draw_vocabulary_card_placeholder(card_index);
    log_printf("[ui] card refresh start\r\n");

    if (epaper_2in15g_display(display_frame_buffer)) {
        log_printf("[ui] card refresh done busy=%lu\r\n",
                   (unsigned long)(epaper_2in15g_busy_level() ? 1 : 0));
        vTaskDelay(pdMS_TO_TICKS(10000));
        epaper_2in15g_sleep();
        *panel_awake = false;
        log_printf("[ui] panel sleep\r\n");
    } else {
        log_printf("[ui] card refresh timeout busy=%lu\r\n",
                   (unsigned long)(epaper_2in15g_busy_level() ? 1 : 0));
    }
}

static void ui_task(void *params) {
    (void)params;

    bool panel_awake = false;
    size_t current_card_index = 0;

    while (true) {
        display_request_t request;

        if (xQueueReceive(display_queue, &request, portMAX_DELAY) != pdPASS) {
            continue;
        }

        switch (request.type) {
        case DISPLAY_REQUEST_TEST_PATTERN:
            log_printf("[ui] test pattern requested\r\n");

            if (!prepare_display_panel()) {
                break;
            }

            panel_awake = true;
            draw_display_test_pattern();
            log_printf("[ui] display refresh start\r\n");

            if (epaper_2in15g_display(display_frame_buffer)) {
                log_printf("[ui] display refresh done busy=%lu\r\n",
                           (unsigned long)(epaper_2in15g_busy_level() ? 1 : 0));
                vTaskDelay(pdMS_TO_TICKS(10000));
                epaper_2in15g_sleep();
                panel_awake = false;
                log_printf("[ui] panel sleep\r\n");
            } else {
                log_printf("[ui] display timeout busy=%lu\r\n",
                           (unsigned long)(epaper_2in15g_busy_level() ? 1 : 0));
            }
            break;

        case DISPLAY_REQUEST_CLEAR_WHITE:
            log_printf("[ui] clear white requested\r\n");

            if (!prepare_display_panel()) {
                break;
            }

            panel_awake = true;
            log_printf("[ui] clear refresh start\r\n");

            if (epaper_2in15g_clear(EPAPER_2IN15G_WHITE)) {
                log_printf("[ui] clear refresh done busy=%lu\r\n",
                           (unsigned long)(epaper_2in15g_busy_level() ? 1 : 0));
                vTaskDelay(pdMS_TO_TICKS(10000));
                epaper_2in15g_sleep();
                panel_awake = false;
                log_printf("[ui] panel sleep\r\n");
            } else {
                log_printf("[ui] clear timeout busy=%lu\r\n",
                           (unsigned long)(epaper_2in15g_busy_level() ? 1 : 0));
            }
            break;

        case DISPLAY_REQUEST_SLEEP:
            if (panel_awake) {
                epaper_2in15g_sleep();
                panel_awake = false;
                log_printf("[ui] panel sleep requested\r\n");
            } else {
                log_printf("[ui] panel already asleep\r\n");
            }
            break;

        case DISPLAY_REQUEST_CARD_SHOW:
            current_card_index = request.card_index % vocabulary_card_count();
            log_printf("[ui] card show requested\r\n");
            refresh_vocabulary_card(current_card_index, &panel_awake);
            break;
        }
    }
}

static void print_console_help(void) {
    log_printf("[console] commands: help | stats | diag on/off | screen test/clear/sleep | card show/next/prev (LF/CRLF)\r\n");
}

static void print_console_stats(void) {
    log_printf("[console] tick=%lu detail_logs=%lu queue=%lu/%lu display_queue=%lu/%lu stack_p=%lu stack_c=%lu stack_e=%lu stack_ui=%lu stack_cli=%lu stack_m=%lu\r\n",
               (unsigned long)xTaskGetTickCount(),
               (unsigned long)(detail_logs_enabled ? 1 : 0),
               (unsigned long)uxQueueMessagesWaiting(sample_queue),
               (unsigned long)uxQueueSpacesAvailable(sample_queue),
               (unsigned long)uxQueueMessagesWaiting(display_queue),
               (unsigned long)uxQueueSpacesAvailable(display_queue),
               (unsigned long)uxTaskGetStackHighWaterMark(producer_task_handle),
               (unsigned long)uxTaskGetStackHighWaterMark(consumer_task_handle),
               (unsigned long)uxTaskGetStackHighWaterMark(event_task_handle),
               (unsigned long)uxTaskGetStackHighWaterMark(ui_task_handle),
               (unsigned long)uxTaskGetStackHighWaterMark(console_task_handle),
               (unsigned long)uxTaskGetStackHighWaterMark(monitor_task_handle));
}

static void handle_screen_command(const char *args) {
    while ((*args == ' ') || (*args == '\t')) {
        args++;
    }

    if (strcmp(args, "test") == 0) {
        send_display_request(DISPLAY_REQUEST_TEST_PATTERN);
        return;
    }

    if (strcmp(args, "clear") == 0) {
        send_display_request(DISPLAY_REQUEST_CLEAR_WHITE);
        return;
    }

    if (strcmp(args, "sleep") == 0) {
        send_display_request(DISPLAY_REQUEST_SLEEP);
        return;
    }

    log_printf("[console] usage: screen test | screen clear | screen sleep\r\n");
}

static void handle_card_command(const char *args) {
    static size_t target_card_index = 0;

    while ((*args == ' ') || (*args == '\t')) {
        args++;
    }

    if (strcmp(args, "show") == 0) {
        send_card_display_request("card show", target_card_index);
        return;
    }

    if (strcmp(args, "next") == 0) {
        target_card_index = (target_card_index + 1) % vocabulary_card_count();
        send_card_display_request("card next", target_card_index);
        return;
    }

    if (strcmp(args, "prev") == 0) {
        target_card_index =
            (target_card_index + vocabulary_card_count() - 1) % vocabulary_card_count();
        send_card_display_request("card prev", target_card_index);
        return;
    }

    log_printf("[console] usage: card show | card next | card prev\r\n");
}

static void handle_console_command(char *line) {
    while ((*line == ' ') || (*line == '\t')) {
        line++;
    }

    if (*line == '\0') {
        return;
    }

    if (strcmp(line, "help") == 0) {
        print_console_help();
        return;
    }

    if (strcmp(line, "stats") == 0) {
        print_console_stats();
        return;
    }

    if ((strcmp(line, "diag off") == 0) || (strcmp(line, "quiet") == 0)) {
        detail_logs_enabled = false;
        log_printf("[console] diagnostic logs disabled; console and ui output remain visible\r\n");
        return;
    }

    if ((strcmp(line, "diag on") == 0) || (strcmp(line, "verbose") == 0)) {
        detail_logs_enabled = true;
        log_printf("[console] diagnostic logs enabled\r\n");
        return;
    }

    if (strncmp(line, "screen", 6) == 0) {
        const char next = line[6];

        if ((next == '\0') || (next == ' ') || (next == '\t')) {
            handle_screen_command(line + 6);
            return;
        }
    }

    if (strncmp(line, "card", 4) == 0) {
        const char next = line[4];

        if ((next == '\0') || (next == ' ') || (next == '\t')) {
            handle_card_command(line + 4);
            return;
        }
    }

    log_printf("[console] unknown command: %s\r\n", line);
    print_console_help();
}

static void console_task(void *params) {
    (void)params;

    char line[CONSOLE_LINE_LENGTH] = { 0 };
    size_t length = 0;

    print_console_help();

    while (true) {
        const int input = getchar_timeout_us(0);

        if (input == PICO_ERROR_TIMEOUT) {
            vTaskDelay(pdMS_TO_TICKS(CONSOLE_POLL_MS));
            continue;
        }

        if ((input == '\r') || (input == '\n')) {
            line[length] = '\0';
            handle_console_command(line);
            length = 0;
            line[0] = '\0';
            continue;
        }

        if ((input == '\b') || (input == 0x7f)) {
            if (length > 0) {
                length--;
                line[length] = '\0';
            }

            continue;
        }

        if ((input < 0x20) || (input > 0x7e)) {
            continue;
        }

        if (length < (CONSOLE_LINE_LENGTH - 1)) {
            line[length++] = (char)input;
        } else {
            length = 0;
            line[0] = '\0';
            log_printf("[console] line too long, dropped\r\n");
        }
    }
}

static void monitor_task(void *params) {
    (void)params;

    while (true) {
        if (!detail_logs_enabled) {
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        log_printf("[monitor] tick=%lu stack_p=%lu stack_c=%lu stack_e=%lu stack_ui=%lu stack_cli=%lu stack_m=%lu queue=%lu/%lu display_queue=%lu/%lu notify=task-local\r\n",
                   (unsigned long)xTaskGetTickCount(),
                   (unsigned long)uxTaskGetStackHighWaterMark(producer_task_handle),
                   (unsigned long)uxTaskGetStackHighWaterMark(consumer_task_handle),
                   (unsigned long)uxTaskGetStackHighWaterMark(event_task_handle),
                   (unsigned long)uxTaskGetStackHighWaterMark(ui_task_handle),
                   (unsigned long)uxTaskGetStackHighWaterMark(console_task_handle),
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
    create_task_or_panic(console_task,
                         "console",
                         DEFAULT_TASK_STACK_WORDS,
                         tskIDLE_PRIORITY + 1,
                         &console_task_handle);
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
    const BaseType_t display_request_sent = xQueueOverwrite(display_queue, &initial_display_request);
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
