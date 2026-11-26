#include "rate_limit.h"

#include <stddef.h>

static void bucket_set(rate_bucket_t *bucket, uint32_t capacity,
                       uint32_t refill_per_second, uint32_t now)
{
    bucket->capacity = capacity;
    bucket->tokens = capacity;
    bucket->refill_per_second = refill_per_second;
    bucket->last_tick = now;
}

static int bucket_allow(rate_bucket_t *bucket, uint32_t now,
                        uint32_t tick_hz, uint32_t cost)
{
    if (bucket->capacity == 0u || tick_hz == 0u) {
        return 0;
    }
    uint32_t elapsed = now - bucket->last_tick; /* wrap-safe unsigned subtraction */
    if (elapsed != 0u && bucket->refill_per_second != 0u) {
        uint64_t add = ((uint64_t)elapsed * bucket->refill_per_second) / tick_hz;
        if (add != 0u) {
            uint64_t total = (uint64_t)bucket->tokens + add;
            bucket->tokens = total > bucket->capacity ? bucket->capacity : (uint32_t)total;
            bucket->last_tick = now;
        }
    }
    if (cost == 0u) {
        cost = 1u;
    }
    if (bucket->tokens < cost) {
        return 0;
    }
    bucket->tokens -= cost;
    return 1;
}

void rate_limiter_init(rate_limiter_t *limiter, uint32_t tick_hz, uint32_t now)
{
    if (limiter == NULL) return;
    limiter->tick_hz = tick_hz;
    limiter->now_tick = now;
    bucket_set(&limiter->global, 0u, 0u, now);
    for (uint32_t i = 0; i < RATE_MAX_SERVICES; i++) {
        bucket_set(&limiter->service[i], 0u, 0u, now);
    }
    for (uint32_t i = 0; i < RATE_MAX_SOURCES; i++) {
        limiter->source[i].key = 0u;
        limiter->source[i].valid = 0u;
        bucket_set(&limiter->source[i].bucket, 0u, 0u, now);
    }
}

void rate_limiter_set_global(rate_limiter_t *limiter, uint32_t capacity,
                             uint32_t refill_per_second)
{
    if (limiter != NULL) {
        bucket_set(&limiter->global, capacity, refill_per_second, limiter->now_tick);
    }
}

int rate_limiter_set_service(rate_limiter_t *limiter, uint32_t id,
                             uint32_t capacity, uint32_t refill_per_second)
{
    if (limiter == NULL || id >= RATE_MAX_SERVICES) return 0;
    bucket_set(&limiter->service[id], capacity, refill_per_second, limiter->now_tick);
    return 1;
}

void rate_limiter_set_now(rate_limiter_t *limiter, uint32_t now)
{
    if (limiter != NULL) limiter->now_tick = now;
}

static rate_source_entry_t *source_entry(rate_limiter_t *limiter, uint32_t key)
{
    uint32_t start = (key ^ (key >> 16)) % RATE_MAX_SOURCES;
    rate_source_entry_t *empty = NULL;
    for (uint32_t probe = 0; probe < 2u; probe++) {
        rate_source_entry_t *entry = &limiter->source[(start + probe) % RATE_MAX_SOURCES];
        if (entry->valid && entry->key == key) return entry;
        if (!entry->valid && empty == NULL) empty = entry;
    }
    if (empty == NULL) empty = &limiter->source[start];
    empty->key = key;
    empty->valid = 1u;
    bucket_set(&empty->bucket, limiter->global.capacity,
               limiter->global.refill_per_second, limiter->now_tick);
    return empty;
}

uint32_t rate_limiter_vm_helper(void *opaque, uint32_t helper_id,
                                const uint32_t regs[VM_NUM_REGS], int *ok)
{
    rate_limiter_t *limiter = (rate_limiter_t *)opaque;
    if (ok != NULL) *ok = 0;
    if (limiter == NULL || regs == NULL || limiter->tick_hz == 0u) return 0u;

    rate_bucket_t *bucket;
    uint32_t cost;
    switch (helper_id) {
    case VM_HELPER_RATE_GLOBAL:
        bucket = &limiter->global;
        cost = regs[1];
        break;
    case VM_HELPER_RATE_SERVICE:
        if (regs[1] >= RATE_MAX_SERVICES) return 0u;
        bucket = &limiter->service[regs[1]];
        cost = 1u;
        break;
    case VM_HELPER_RATE_SOURCE: {
        rate_source_entry_t *entry = source_entry(limiter, regs[1]);
        bucket = &entry->bucket;
        cost = 1u;
        break;
    }
    default:
        return 0u;
    }
    if (ok != NULL) *ok = 1;
    return bucket_allow(bucket, limiter->now_tick, limiter->tick_hz, cost) ? 1u : 0u;
}
