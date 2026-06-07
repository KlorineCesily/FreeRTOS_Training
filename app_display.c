#include "app_display.h"

#include <stdbool.h>
#include <stdint.h>

#include "app_card.h"
#include "app_draw.h"
#include "epaper_2in15g.h"

#include "queue.h"
#include "task.h"

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

enum {
    DISPLAY_QUEUE_LENGTH = 1,
};

static QueueHandle_t display_queue = NULL;
static TaskHandle_t display_task_handle = NULL;
static app_display_log_fn_t display_log = NULL;
static uint8_t display_frame_buffer[EPAPER_2IN15G_BUFFER_SIZE];

static void display_task(void *params);

static void create_task_or_panic(TaskFunction_t task_code,
                                 const char *name,
                                 configSTACK_DEPTH_TYPE stack_depth,
                                 UBaseType_t priority,
                                 TaskHandle_t *task_handle) {
    const BaseType_t result = xTaskCreate(task_code, name, stack_depth, NULL, priority, task_handle);
    configASSERT(result == pdPASS);
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

    configASSERT(display_queue != NULL);

    const BaseType_t result = xQueueOverwrite(display_queue, &request);
    configASSERT(result == pdPASS);

    display_log("[console] display %s request accepted (latest wins)\r\n",
                display_request_name(type));
}

static bool prepare_display_panel(void) {
    epaper_2in15g_init_io();
    display_log("[ui] io ready busy=%lu\r\n",
                (unsigned long)(epaper_2in15g_busy_level() ? 1 : 0));

    if (!epaper_2in15g_init_panel()) {
        display_log("[ui] panel init timeout busy=%lu\r\n",
                    (unsigned long)(epaper_2in15g_busy_level() ? 1 : 0));
        return false;
    }

    display_log("[ui] panel ready busy=%lu\r\n",
                (unsigned long)(epaper_2in15g_busy_level() ? 1 : 0));
    return true;
}

static void refresh_vocabulary_card(size_t card_index, bool *panel_awake) {
    const vocabulary_card_t *card = app_card_get(card_index);

    display_log("[ui] card %lu/%lu word=%s %s\r\n",
                (unsigned long)(card_index + 1),
                (unsigned long)app_card_count(),
                card->word,
                card->phonetic);
    display_log("[ui] meaning=%s\r\n", card->meaning);
    display_log("[ui] note=%s\r\n", card->note);
    display_log("[ui] example=%s\r\n", card->example);

    if (!prepare_display_panel()) {
        return;
    }

    *panel_awake = true;
    app_draw_vocabulary_card(display_frame_buffer, card, card_index, app_card_count());
    display_log("[ui] card refresh start\r\n");

    if (epaper_2in15g_display(display_frame_buffer)) {
        display_log("[ui] card refresh done busy=%lu\r\n",
                    (unsigned long)(epaper_2in15g_busy_level() ? 1 : 0));
        vTaskDelay(pdMS_TO_TICKS(10000));
        epaper_2in15g_sleep();
        *panel_awake = false;
        display_log("[ui] panel sleep\r\n");
    } else {
        display_log("[ui] card refresh timeout busy=%lu\r\n",
                    (unsigned long)(epaper_2in15g_busy_level() ? 1 : 0));
    }
}

void app_display_init(app_display_log_fn_t log_fn) {
    configASSERT(log_fn != NULL);
    configASSERT(display_queue == NULL);

    display_log = log_fn;
    display_queue = xQueueCreate(DISPLAY_QUEUE_LENGTH, sizeof(display_request_t));
    configASSERT(display_queue != NULL);
}

void app_display_start(configSTACK_DEPTH_TYPE stack_depth, UBaseType_t priority) {
    configASSERT(display_queue != NULL);
    configASSERT(display_task_handle == NULL);

    create_task_or_panic(display_task,
                         "ui",
                         stack_depth,
                         priority,
                         &display_task_handle);
}

void app_display_request_test_pattern(void) {
    send_display_request(DISPLAY_REQUEST_TEST_PATTERN);
}

void app_display_request_clear_white(void) {
    send_display_request(DISPLAY_REQUEST_CLEAR_WHITE);
}

void app_display_request_sleep(void) {
    send_display_request(DISPLAY_REQUEST_SLEEP);
}

void app_display_request_card(const char *command_name, size_t card_index) {
    const display_request_t request = {
        .type = DISPLAY_REQUEST_CARD_SHOW,
        .card_index = card_index,
    };

    configASSERT(display_queue != NULL);

    const BaseType_t result = xQueueOverwrite(display_queue, &request);
    configASSERT(result == pdPASS);

    display_log("[console] %s request accepted card=%lu/%lu (latest wins)\r\n",
                command_name,
                (unsigned long)(card_index + 1),
                (unsigned long)app_card_count());
}

UBaseType_t app_display_queue_messages_waiting(void) {
    configASSERT(display_queue != NULL);
    return uxQueueMessagesWaiting(display_queue);
}

UBaseType_t app_display_queue_spaces_available(void) {
    configASSERT(display_queue != NULL);
    return uxQueueSpacesAvailable(display_queue);
}

UBaseType_t app_display_stack_high_water_mark(void) {
    configASSERT(display_task_handle != NULL);
    return uxTaskGetStackHighWaterMark(display_task_handle);
}

static void display_task(void *params) {
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
            display_log("[ui] test pattern requested\r\n");

            if (!prepare_display_panel()) {
                break;
            }

            panel_awake = true;
            app_draw_test_pattern(display_frame_buffer);
            display_log("[ui] display refresh start\r\n");

            if (epaper_2in15g_display(display_frame_buffer)) {
                display_log("[ui] display refresh done busy=%lu\r\n",
                            (unsigned long)(epaper_2in15g_busy_level() ? 1 : 0));
                vTaskDelay(pdMS_TO_TICKS(10000));
                epaper_2in15g_sleep();
                panel_awake = false;
                display_log("[ui] panel sleep\r\n");
            } else {
                display_log("[ui] display timeout busy=%lu\r\n",
                            (unsigned long)(epaper_2in15g_busy_level() ? 1 : 0));
            }
            break;

        case DISPLAY_REQUEST_CLEAR_WHITE:
            display_log("[ui] clear white requested\r\n");

            if (!prepare_display_panel()) {
                break;
            }

            panel_awake = true;
            display_log("[ui] clear refresh start\r\n");

            if (epaper_2in15g_clear(EPAPER_2IN15G_WHITE)) {
                display_log("[ui] clear refresh done busy=%lu\r\n",
                            (unsigned long)(epaper_2in15g_busy_level() ? 1 : 0));
                vTaskDelay(pdMS_TO_TICKS(10000));
                epaper_2in15g_sleep();
                panel_awake = false;
                display_log("[ui] panel sleep\r\n");
            } else {
                display_log("[ui] clear timeout busy=%lu\r\n",
                            (unsigned long)(epaper_2in15g_busy_level() ? 1 : 0));
            }
            break;

        case DISPLAY_REQUEST_SLEEP:
            if (panel_awake) {
                epaper_2in15g_sleep();
                panel_awake = false;
                display_log("[ui] panel sleep requested\r\n");
            } else {
                display_log("[ui] panel already asleep\r\n");
            }
            break;

        case DISPLAY_REQUEST_CARD_SHOW:
            current_card_index = request.card_index % app_card_count();
            display_log("[ui] card show requested\r\n");
            refresh_vocabulary_card(current_card_index, &panel_awake);
            break;
        }
    }
}
