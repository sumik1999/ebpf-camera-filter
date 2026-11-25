#ifndef VM_SLOT_H
#define VM_SLOT_H

/*
 * vm_slot -- the ONLY way to install a bytecode program into a hook (D9).
 *
 * A slot belongs to one hook and remembers which context that hook
 * delivers (VM_CTX_ETH for the driver hook, VM_CTX_IPV4 for the lwIP
 * hook). vm_slot_install() enforces CLAUDE.md rule 2:
 *
 *   1. the program must pass vm_verify()                 (else refused)
 *   2. its ctx must match the slot's ctx (ISA v1 Q5)    (else refused)
 *
 * On any refusal the slot keeps the program it had before, so a bad
 * update never leaves a hook half-configured.
 *
 * Why this matters: the interpreter trusts the verifier (it does not
 * range-check register numbers, ISA v1 decision Q3). Before this file,
 * nothing stopped a caller from installing an unverified program.
 *
 * Concurrency: the program pointer is `volatile` and is written with one
 * aligned 32-bit store, which is atomic on Cortex-M33. The hook reads it
 * ONCE per packet (vm_slot_get) into a local variable, so a packet is
 * always filtered by one whole program, old or new, never a mix.
 *
 * Lifetime rule: the program (its vm_program_t and instruction array)
 * must not change and must stay valid while installed. Our programs are
 * `static const` (in flash), which satisfies this. Changing a program
 * after it was verified would defeat the verifier.
 *
 * No heap: a slot is a small struct the hook owns as a static variable.
 */

#include <stdint.h>
#include "vm_isa.h"
#include "vm_verifier.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const vm_program_t *volatile prog;  /* NULL = no program installed */
    uint8_t ctx;                        /* context this hook delivers  */
} vm_slot_t;

typedef enum {
    VM_SLOT_OK = 0,
    VM_SLOT_ERR_NULL,     /* slot or program pointer is NULL            */
    VM_SLOT_ERR_VERIFY,   /* program failed vm_verify() (see *why)      */
    VM_SLOT_ERR_CTX,      /* program was written for a different hook   */
} vm_slot_result_t;

/* Start empty, for a hook that delivers `ctx` (VM_CTX_ETH / VM_CTX_IPV4). */
void vm_slot_init(vm_slot_t *slot, uint8_t ctx);

/* Verify, check ctx, then publish. `why` (optional) receives the verifier
 * result, so a caller can print vm_verify_strerror(*why). */
vm_slot_result_t vm_slot_install(vm_slot_t *slot, const vm_program_t *prog,
                                 vm_verify_result_t *why);

/* Remove the program: the hook then passes every packet. */
void vm_slot_clear(vm_slot_t *slot);

/* The program to run for this packet, or NULL. Read it once per packet. */
static inline const vm_program_t *vm_slot_get(const vm_slot_t *slot)
{
    return slot->prog;
}

const char *vm_slot_strerror(vm_slot_result_t r);

#ifdef __cplusplus
}
#endif

#endif /* VM_SLOT_H */
