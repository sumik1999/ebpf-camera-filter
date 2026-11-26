#ifndef FILTER_FREERTOS_H
#define FILTER_FREERTOS_H

#include "filter_management_task.h"

#ifdef FILTER_USE_FREERTOS
#include "FreeRTOS.h"
#include "task.h"

#ifdef __cplusplus
extern "C" {
#endif

void filter_freertos_enter_critical(void *opaque);
void filter_freertos_exit_critical(void *opaque);
void filter_freertos_idle(void *opaque);

BaseType_t filter_freertos_start_management(filter_management_task_t *management,
                                            const char *name,
                                            uint16_t stack_depth_words,
                                            UBaseType_t priority,
                                            TaskHandle_t *handle);

#ifdef __cplusplus
}
#endif
#endif /* FILTER_USE_FREERTOS */

#endif /* FILTER_FREERTOS_H */
