#ifndef VM_INTERPRETER_H
#define VM_INTERPRETER_H

#include <stdint.h>
#include "vm_isa.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t insns_executed;  /* instructions started on every exit path, including
                                 a load that faulted; VM_MAX_STEPS on a budget stop */
    uint32_t aborted_oob;     /* aborted: out-of-bounds packet access      */
    uint32_t aborted_budget;  /* aborted: VM_MAX_STEPS step budget exceeded */
    uint32_t aborted_helper;  /* aborted: helper unavailable/refused          */
} vm_run_stats_t;

typedef uint32_t (*vm_helper_call_fn)(void *opaque, uint32_t helper_id,
                                      const uint32_t regs[VM_NUM_REGS], int *ok);

typedef struct {
    vm_helper_call_fn call;
    void *opaque;
    uint32_t helper_mask;
} vm_exec_env_t;

/*
 * Execute a *verified* program (vm_verify() must have already
 * returned VM_VERIFY_OK for this program) against one packet buffer.
 * Register indices are NOT range-checked here (ISA v1 decision Q3):
 * running an unverified program is outside the safety guarantee.
 *
 * pkt_data / pkt_len describe the raw frame exactly as delivered by
 * the caller -- zero-copy: no packet bytes are ever copied into or
 * out of the VM, only a pointer + length are passed in.
 *
 * Returns VM_VERDICT_PASS or VM_VERDICT_DROP. Every runtime fault
 * (out-of-bounds packet access, step-budget exceeded, NULL inputs)
 * fails SAFE, i.e. returns VM_VERDICT_DROP -- it never returns PASS
 * on an error path.
 *
 * This function performs NO heap allocation (fixed on-stack register
 * file only), uses a bounded number of steps (VM_MAX_STEPS), and is
 * safe to call from FreeRTOS task context or even from within a
 * driver callback, since it has fully deterministic, bounded cost.
 */
int vm_interpreter_run(const vm_program_t *prog,
                        const uint8_t *pkt_data,
                        uint32_t pkt_len,
                        vm_run_stats_t *stats /* optional, may be NULL */);

/* ISA v2 execution with bounded native helpers. Legacy callers use the
 * wrapper above, which supplies no helper environment. */
int vm_interpreter_run_ex(const vm_program_t *prog,
                          const uint8_t *ctx_data,
                          uint32_t ctx_len,
                          const vm_exec_env_t *env,
                          vm_run_stats_t *stats);

#ifdef __cplusplus
}
#endif

#endif /* VM_INTERPRETER_H */
