#include "filter_freertos.h"

#ifdef FILTER_USE_FREERTOS

void filter_freertos_enter_critical(void *opaque)
{
    (void)opaque;
    taskENTER_CRITICAL();
}

void filter_freertos_exit_critical(void *opaque)
{
    (void)opaque;
    taskEXIT_CRITICAL();
}

void filter_freertos_idle(void *opaque)
{
    (void)opaque;
    vTaskDelay(pdMS_TO_TICKS(1u));
}

uint32_t filter_freertos_tick(void *opaque)
{
    (void)opaque;
    return (uint32_t)xTaskGetTickCount();
}

BaseType_t filter_freertos_start_management(filter_management_task_t *management,
                                            const char *name,
                                            uint16_t stack_depth_words,
                                            UBaseType_t priority,
                                            TaskHandle_t *handle)
{
    if (management == NULL || name == NULL || stack_depth_words == 0u) {
        return pdFAIL;
    }
    return xTaskCreate(filter_management_task, name, stack_depth_words,
                       management, priority, handle);
}

#endif /* FILTER_USE_FREERTOS */
