#include "app_log.h"

#include <stdarg.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "semphr.h"

static SemaphoreHandle_t log_mutex = NULL;

void app_log_init(void) {
    if (log_mutex == NULL) {
        log_mutex = xSemaphoreCreateMutex();
        configASSERT(log_mutex != NULL);
    }
}

void app_log_printf(const char *format, ...) {
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
