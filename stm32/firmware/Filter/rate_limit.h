#ifndef RATE_LIMIT_H
#define RATE_LIMIT_H

#include <stdint.h>
#include "vm_interpreter.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RATE_MAX_SERVICES 4u
#define RATE_MAX_SOURCES  16u

typedef struct {
    uint32_t capacity;
    uint32_t tokens;
    uint32_t refill_per_second;
    uint32_t last_tick;
} rate_bucket_t;

typedef struct {
    uint32_t key;
    uint8_t valid;
    uint8_t reserved[3];
    rate_bucket_t bucket;
} rate_source_entry_t;

typedef struct {
    uint32_t tick_hz;
    uint32_t now_tick;
    rate_bucket_t global;
    rate_bucket_t service[RATE_MAX_SERVICES];
    rate_source_entry_t source[RATE_MAX_SOURCES];
} rate_limiter_t;

void rate_limiter_init(rate_limiter_t *limiter, uint32_t tick_hz, uint32_t now);
void rate_limiter_set_global(rate_limiter_t *limiter, uint32_t capacity,
                             uint32_t refill_per_second);
int rate_limiter_set_service(rate_limiter_t *limiter, uint32_t id,
                             uint32_t capacity, uint32_t refill_per_second);
void rate_limiter_set_now(rate_limiter_t *limiter, uint32_t now);

/* vm_exec_env_t callback. For GLOBAL, r1 is the token cost. For SERVICE
 * and SOURCE, r1 is the bounded service ID or source key and cost is one
 * packet. Separate byte-rate helpers can be added without ambiguity. */
uint32_t rate_limiter_vm_helper(void *opaque, uint32_t helper_id,
                                const uint32_t regs[VM_NUM_REGS], int *ok);

#ifdef __cplusplus
}
#endif

#endif /* RATE_LIMIT_H */
