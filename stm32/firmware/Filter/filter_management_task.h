#ifndef FILTER_MANAGEMENT_TASK_H
#define FILTER_MANAGEMENT_TASK_H

#include <stdint.h>
#include "filter_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FILTER_MGMT_QUEUE_DEPTH 2u

typedef void (*filter_mgmt_idle_fn)(void *opaque);
typedef void (*filter_mgmt_result_fn)(void *opaque, filter_update_result_t result,
                                      vm_verify_result_t verify_result);

typedef struct {
    uint16_t len;
    volatile uint8_t state; /* 0 free, 1 writing, 2 ready, 3 processing */
    uint8_t reserved;
    uint8_t data[FILTER_UPDATE_MAX_MSG];
} filter_mgmt_message_t;

typedef struct {
    filter_manager_t *manager;
    filter_mgmt_message_t queue[FILTER_MGMT_QUEUE_DEPTH];
    uint8_t head;
    uint8_t tail;
    uint8_t count;
    volatile uint8_t stop;
    filter_mgmt_idle_fn idle;
    filter_mgmt_result_fn result;
    void *callback_opaque;
} filter_management_task_t;

void filter_management_task_init(filter_management_task_t *task,
                                 filter_manager_t *manager,
                                 filter_mgmt_idle_fn idle,
                                 filter_mgmt_result_fn result,
                                 void *callback_opaque);

/* Copy a complete authenticated package into the bounded queue. Call from
 * task context, not an ISR. Returns 0 if full or oversized. */
int filter_management_submit(filter_management_task_t *task,
                             const uint8_t *message, uint32_t len);

/* Process one queued update. Useful in deterministic tests. */
int filter_management_process_one(filter_management_task_t *task);

/* FreeRTOS-compatible task entry. idle() should block/delay when no work. */
void filter_management_task(void *argument);

#ifdef __cplusplus
}
#endif

#endif /* FILTER_MANAGEMENT_TASK_H */
