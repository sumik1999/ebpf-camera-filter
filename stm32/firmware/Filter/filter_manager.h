#ifndef FILTER_MANAGER_H
#define FILTER_MANAGER_H

#include <stdint.h>
#include <stdatomic.h>
#include "filter_crypto.h"
#include "rate_limit.h"
#include "vm_meta_abi.h"
#include "vm_verifier.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FILTER_UPDATE_MAGIC       0x32564D46u /* bytes "FMV2" little-endian */
#define FILTER_UPDATE_FORMAT      1u
#define FILTER_UPDATE_HEADER_LEN  60u
#define FILTER_UPDATE_MAX_MSG     (FILTER_UPDATE_HEADER_LEN + 8u * VM_MAX_INSNS)
#define FILTER_PROGRAM_BANKS      3u
#define FILTER_AUTH_KEY_MAX       32u

typedef enum {
    FILTER_UPDATE_OK = 0,
    FILTER_UPDATE_ERR_SHORT,
    FILTER_UPDATE_ERR_MAGIC,
    FILTER_UPDATE_ERR_FORMAT,
    FILTER_UPDATE_ERR_DEVICE,
    FILTER_UPDATE_ERR_VERSION,
    FILTER_UPDATE_ERR_CONTEXT,
    FILTER_UPDATE_ERR_LENGTH,
    FILTER_UPDATE_ERR_GENERATION,
    FILTER_UPDATE_ERR_CRC,
    FILTER_UPDATE_ERR_AUTH,
    FILTER_UPDATE_ERR_VERIFY,
    FILTER_UPDATE_ERR_STORAGE,
} filter_update_result_t;

typedef void (*filter_critical_fn)(void *opaque);

typedef struct {
    uint32_t accepted_updates;
    uint32_t rejected_updates;
    uint32_t auth_failures;
    uint32_t replay_failures;
    uint32_t rollbacks;
    uint32_t vm_faults;
} filter_manager_stats_t;

/* Immutable after publication. RX acquires this pointer once, ensuring the
 * program and all authenticated execution attributes belong to one update. */
typedef struct {
    vm_program_t program;
    uint32_t helper_mask;
    uint32_t generation;
    uint32_t hash;
    atomic_uint readers; /* RX references preventing premature bank reuse */
} filter_active_t;

typedef struct {
    vm_insn_t insns[FILTER_PROGRAM_BANKS][VM_MAX_INSNS];
    filter_active_t bank[FILTER_PROGRAM_BANKS];
    filter_active_t recovery_state;
    _Atomic(const filter_active_t *) active;
    const filter_active_t *previous;
    uint32_t highest_generation;
    uint32_t device_id;
    uint8_t auth_key[FILTER_AUTH_KEY_MAX];
    uint8_t auth_key_len;
    filter_critical_fn enter_critical;
    filter_critical_fn exit_critical;
    void *critical_opaque;
    rate_limiter_t limiter;
    filter_manager_stats_t stats;
} filter_manager_t;

/* Inspect the current descriptor. RX execution uses an internal referenced
 * snapshot; callers must not retain this unreferenced status pointer across
 * concurrent updates. */
const filter_active_t *filter_manager_active(const filter_manager_t *manager);

void filter_manager_init(filter_manager_t *manager, uint32_t device_id,
                         const uint8_t *key, uint32_t key_len,
                         const vm_program_t *recovery,
                         filter_critical_fn enter_critical,
                         filter_critical_fn exit_critical,
                         void *critical_opaque,
                         uint32_t tick_hz);

filter_update_result_t filter_manager_install(filter_manager_t *manager,
                                               const uint8_t *message,
                                               uint32_t message_len,
                                               vm_verify_result_t *why);
int filter_manager_rollback(filter_manager_t *manager);
void filter_manager_recover(filter_manager_t *manager);

/* Parse, encode, and execute the currently active metadata program. */
int filter_manager_run(filter_manager_t *manager, const uint8_t *frame,
                       uint32_t frame_len, uint32_t now_tick,
                       vm_run_stats_t *run_stats);

/* Host/management-side helper used by tests and tooling equivalents. */
uint32_t filter_update_build(uint8_t *out, uint32_t out_capacity,
                             uint32_t device_id, uint32_t generation,
                             uint32_t helper_mask, const vm_insn_t *insns,
                             uint16_t n_insns, const uint8_t *key,
                             uint32_t key_len);

const char *filter_update_strerror(filter_update_result_t result);

#ifdef __cplusplus
}
#endif

#endif /* FILTER_MANAGER_H */
