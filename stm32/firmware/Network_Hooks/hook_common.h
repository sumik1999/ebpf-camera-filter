#ifndef HOOK_COMMON_H
#define HOOK_COMMON_H

/*
 * hook_common.h -- what the driver hook and the lwIP hook share.
 *
 * A hook runs AT MOST ONE filter on each packet:
 *   - a verified bytecode program (installed through its vm_slot), or
 *   - a native C filter function (the baseline we compare against), or
 *   - nothing (every packet passes).
 * Installing one kind removes the other, so there is never a doubt about
 * which filter produced a verdict.
 *
 * No heap, no RTOS calls, no HAL: just a pointer and a few counters, so
 * the same code runs on the STM32, in QEMU and in the host tests.
 *
 * Counters are written only by the one task that runs the hook (the RX
 * task, or tcpip_thread for the lwIP hook) and may be read by any task.
 */

#include <stdint.h>
#include "vm_isa.h"
#include "vm_slot.h"
#include "vm_interpreter.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A native filter: same contract as the VM (returns VM_VERDICT_PASS or
 * VM_VERDICT_DROP, and must DROP on a frame too short to parse). */
typedef int (*hook_native_fn)(const uint8_t *pkt, uint32_t len);

typedef struct {
    volatile uint32_t rx_total;           /* packets seen by the hook            */
    volatile uint32_t rx_passed;          /* verdict PASS from a filter          */
    volatile uint32_t rx_dropped;         /* verdict DROP from a filter          */
    volatile uint32_t rx_no_filter_pass;  /* passed because no filter installed
                                             (was rx_dropped_no_prog, D8)        */
} hook_stats_t;

typedef enum {
    HOOK_FILTER_NONE = 0,
    HOOK_FILTER_VM,
    HOOK_FILTER_NATIVE,
} hook_filter_kind_t;

typedef struct {
    vm_slot_t               slot;    /* bytecode program, verified on install */
    volatile hook_native_fn native;  /* native filter, or NULL                */
    hook_stats_t            stats;
} hook_t;

static inline void hook_init(hook_t *h, uint8_t ctx)
{
    vm_slot_init(&h->slot, ctx);
    h->native = (hook_native_fn)0;
    h->stats.rx_total = 0;
    h->stats.rx_passed = 0;
    h->stats.rx_dropped = 0;
    h->stats.rx_no_filter_pass = 0;
}

/* Install a bytecode program (verified + ctx-checked by vm_slot). On
 * success the native filter is removed; on failure nothing changes. */
static inline vm_slot_result_t hook_install_vm(hook_t *h, const vm_program_t *prog,
                                               vm_verify_result_t *why)
{
    vm_slot_result_t r = vm_slot_install(&h->slot, prog, why);
    if (r == VM_SLOT_OK) {
        h->native = (hook_native_fn)0;
    }
    return r;
}

/* Use a native C filter instead of bytecode (NULL = no filter). */
static inline void hook_install_native(hook_t *h, hook_native_fn fn)
{
    vm_slot_clear(&h->slot);
    h->native = fn;
}

static inline void hook_clear(hook_t *h)
{
    vm_slot_clear(&h->slot);
    h->native = (hook_native_fn)0;
}

/* Which filter is installed right now (for banners and tests). */
static inline hook_filter_kind_t hook_filter_kind(const hook_t *h)
{
    if (vm_slot_get(&h->slot) != NULL) return HOOK_FILTER_VM;
    if (h->native != (hook_native_fn)0) return HOOK_FILTER_NATIVE;
    return HOOK_FILTER_NONE;
}

/* The per-packet path. Each pointer is read once, so a packet is judged
 * by one complete filter even if another task swaps filters meanwhile. */
static inline int hook_run(hook_t *h, const uint8_t *pkt, uint32_t len)
{
    int verdict;
    const vm_program_t *prog = vm_slot_get(&h->slot);
    hook_native_fn native = h->native;

    h->stats.rx_total++;
    if (prog != NULL) {
        verdict = vm_interpreter_run(prog, pkt, len, (vm_run_stats_t *)0);
    } else if (native != (hook_native_fn)0) {
        verdict = native(pkt, len);
    } else {
        h->stats.rx_no_filter_pass++;
        return VM_VERDICT_PASS;
    }

    if (verdict == VM_VERDICT_PASS) {
        h->stats.rx_passed++;
        return VM_VERDICT_PASS;
    }
    h->stats.rx_dropped++;              /* anything not PASS is DROP (fail safe) */
    return VM_VERDICT_DROP;
}

#ifdef __cplusplus
}
#endif

#endif /* HOOK_COMMON_H */
