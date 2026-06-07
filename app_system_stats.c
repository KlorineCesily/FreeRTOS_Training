#include "app_system_stats.h"

#include "FreeRTOS.h"
#include "portable.h"
#include "task.h"

#include "app_console.h"
#include "app_display.h"
#include "app_events.h"
#include "app_log.h"
#include "app_monitor.h"
#include "app_sample.h"

enum {
    TASK_SNAPSHOT_CAPACITY = 16,
};

static const char *task_state_name(eTaskState state) {
    switch (state) {
    case eRunning:
        return "running";
    case eReady:
        return "ready";
    case eBlocked:
        return "blocked";
    case eSuspended:
        return "suspended";
    case eDeleted:
        return "deleted";
    case eInvalid:
    default:
        return "invalid";
    }
}

void app_system_stats_print_summary(const char *source) {
    const EventBits_t event_bits = app_events_get_bits();

    app_log_printf("[%s] tick=%lu heap_free=%lu heap_min=%lu events=0x%lx busy=%lu dirty=%lu diag=%lu queue=%lu/%lu display_queue=%lu/%lu stack_p=%lu stack_c=%lu stack_e=%lu stack_ui=%lu stack_cli=%lu stack_m=%lu\r\n",
                   source,
                   (unsigned long)xTaskGetTickCount(),
                   (unsigned long)xPortGetFreeHeapSize(),
                   (unsigned long)xPortGetMinimumEverFreeHeapSize(),
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

void app_system_stats_print_tasks(const char *source) {
    TaskStatus_t tasks[TASK_SNAPSHOT_CAPACITY];
    const UBaseType_t expected_tasks = uxTaskGetNumberOfTasks();
    const UBaseType_t captured_tasks = uxTaskGetSystemState(tasks, TASK_SNAPSHOT_CAPACITY, NULL);

    if (captured_tasks == 0) {
        app_log_printf("[%s] task_snapshot unavailable total=%lu capacity=%lu\r\n",
                       source,
                       (unsigned long)expected_tasks,
                       (unsigned long)TASK_SNAPSHOT_CAPACITY);
        return;
    }

    app_log_printf("[%s] task_snapshot captured=%lu total=%lu capacity=%lu\r\n",
                   source,
                   (unsigned long)captured_tasks,
                   (unsigned long)expected_tasks,
                   (unsigned long)TASK_SNAPSHOT_CAPACITY);

    for (UBaseType_t i = 0; i < captured_tasks; i++) {
        const TaskStatus_t *task = &tasks[i];
        const char *name = (task->pcTaskName != NULL) ? task->pcTaskName : "(null)";

        app_log_printf("[%s] task name=%s state=%s prio=%lu base_prio=%lu stack_min=%lu\r\n",
                       source,
                       name,
                       task_state_name(task->eCurrentState),
                       (unsigned long)task->uxCurrentPriority,
                       (unsigned long)task->uxBasePriority,
                       (unsigned long)task->usStackHighWaterMark);
    }
}
