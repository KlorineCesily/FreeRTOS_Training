#include "app_console.h"

#include <stddef.h>
#include <string.h>

#include "pico/error.h"
#include "pico/stdio.h"

#include "app_card.h"
#include "app_display.h"
#include "app_events.h"
#include "app_log.h"
#include "app_monitor.h"
#include "app_sample.h"

enum {
    CONSOLE_LINE_LENGTH = 64,
    CONSOLE_POLL_MS = 20,
};

static TaskHandle_t console_task_handle = NULL;

static void create_task_or_panic(TaskFunction_t task_code,
                                 const char *name,
                                 configSTACK_DEPTH_TYPE stack_depth,
                                 UBaseType_t priority,
                                 TaskHandle_t *task_handle) {
    const BaseType_t result = xTaskCreate(task_code, name, stack_depth, NULL, priority, task_handle);
    configASSERT(result == pdPASS);
}

static void print_console_help(void) {
    app_log_printf("[console] commands: help | stats | diag on/off | screen test/clear/sleep | card show/next/prev (LF/CRLF)\r\n");
}

static void print_console_stats(void) {
    const EventBits_t event_bits = app_events_get_bits();

    app_log_printf("[console] tick=%lu events=0x%lx busy=%lu dirty=%lu diag=%lu queue=%lu/%lu display_queue=%lu/%lu stack_p=%lu stack_c=%lu stack_e=%lu stack_ui=%lu stack_cli=%lu stack_m=%lu\r\n",
                   (unsigned long)xTaskGetTickCount(),
                   (unsigned long)event_bits,
                   (unsigned long)((event_bits & APP_EVENT_DISPLAY_BUSY) ? 1 : 0),
                   (unsigned long)((event_bits & APP_EVENT_CARD_DIRTY) ? 1 : 0),
                   (unsigned long)((event_bits & APP_EVENT_DIAG_ENABLED) ? 1 : 0),
                   (unsigned long)app_sample_queue_messages_waiting(),
                   (unsigned long)app_sample_queue_spaces_available(),
                   (unsigned long)app_display_queue_messages_waiting(),
                   (unsigned long)app_display_queue_spaces_available(),
                   (unsigned long)app_sample_producer_stack_high_water_mark(),
                   (unsigned long)app_sample_consumer_stack_high_water_mark(),
                   (unsigned long)app_sample_event_stack_high_water_mark(),
                   (unsigned long)app_display_stack_high_water_mark(),
                   (unsigned long)app_console_stack_high_water_mark(),
                   (unsigned long)app_monitor_stack_high_water_mark());
}

static void skip_spaces(const char **text) {
    while ((**text == ' ') || (**text == '\t')) {
        (*text)++;
    }
}

static void handle_screen_command(const char *args) {
    skip_spaces(&args);

    if (strcmp(args, "test") == 0) {
        app_display_request_test_pattern();
        return;
    }

    if (strcmp(args, "clear") == 0) {
        app_display_request_clear_white();
        return;
    }

    if (strcmp(args, "sleep") == 0) {
        app_display_request_sleep();
        return;
    }

    app_log_printf("[console] usage: screen test | screen clear | screen sleep\r\n");
}

static void handle_card_command(const char *args) {
    static size_t target_card_index = 0;

    skip_spaces(&args);

    if (strcmp(args, "show") == 0) {
        app_display_request_card("card show", target_card_index);
        return;
    }

    if (strcmp(args, "next") == 0) {
        target_card_index = app_card_next(target_card_index);
        app_display_request_card("card next", target_card_index);
        return;
    }

    if (strcmp(args, "prev") == 0) {
        target_card_index = app_card_prev(target_card_index);
        app_display_request_card("card prev", target_card_index);
        return;
    }

    app_log_printf("[console] usage: card show | card next | card prev\r\n");
}

static void handle_console_command(char *line) {
    const char *command = line;
    skip_spaces(&command);

    if (*command == '\0') {
        return;
    }

    if (strcmp(command, "help") == 0) {
        print_console_help();
        return;
    }

    if (strcmp(command, "stats") == 0) {
        print_console_stats();
        return;
    }

    if ((strcmp(command, "diag off") == 0) || (strcmp(command, "quiet") == 0)) {
        app_sample_set_detail_logs_enabled(false);
        app_log_printf("[console] diagnostic logs disabled; console and ui output remain visible\r\n");
        return;
    }

    if ((strcmp(command, "diag on") == 0) || (strcmp(command, "verbose") == 0)) {
        app_sample_set_detail_logs_enabled(true);
        app_log_printf("[console] diagnostic logs enabled\r\n");
        return;
    }

    if (strncmp(command, "screen", 6) == 0) {
        const char next = command[6];

        if ((next == '\0') || (next == ' ') || (next == '\t')) {
            handle_screen_command(command + 6);
            return;
        }
    }

    if (strncmp(command, "card", 4) == 0) {
        const char next = command[4];

        if ((next == '\0') || (next == ' ') || (next == '\t')) {
            handle_card_command(command + 4);
            return;
        }
    }

    app_log_printf("[console] unknown command: %s\r\n", command);
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
            app_log_printf("[console] line too long, dropped\r\n");
        }
    }
}

void app_console_start(configSTACK_DEPTH_TYPE stack_depth, UBaseType_t priority) {
    configASSERT(console_task_handle == NULL);

    create_task_or_panic(console_task,
                         "console",
                         stack_depth,
                         priority,
                         &console_task_handle);
}

UBaseType_t app_console_stack_high_water_mark(void) {
    configASSERT(console_task_handle != NULL);
    return uxTaskGetStackHighWaterMark(console_task_handle);
}
