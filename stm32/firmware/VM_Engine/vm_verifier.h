#ifndef VM_VERIFIER_H
#define VM_VERIFIER_H

#include <stdint.h>
#include "vm_isa.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    VM_VERIFY_OK = 0,
    VM_VERIFY_ERR_EMPTY,
    VM_VERIFY_ERR_TOO_LONG,
    VM_VERIFY_ERR_BAD_OPCODE,
    VM_VERIFY_ERR_BAD_REG,
    VM_VERIFY_ERR_BAD_JUMP,
    VM_VERIFY_ERR_BACKWARD_JUMP,
    VM_VERIFY_ERR_NO_RET,
    VM_VERIFY_ERR_UNUSED_FIELD,     /* ISA v1: a field the opcode does not use is non-zero */
    VM_VERIFY_ERR_BAD_RET,          /* ISA v1: RET value is not VM_VERDICT_DROP/PASS       */
    VM_VERIFY_ERR_BAD_CTX,          /* ISA v1: ctx is not VM_CTX_ETH or VM_CTX_IPV4        */
} vm_verify_result_t;

/*
 * Statically verify a bytecode program BEFORE it is ever executed.
 * This is the "lightweight verifier" required by Role 1. It enforces:
 *
 *   1. Program is non-empty and within VM_MAX_INSNS  (bounded size),
 *      and declares a known context (VM_CTX_ETH or VM_CTX_IPV4).
 *   2. Every opcode is one this VM actually implements.
 *   3. Every register operand is in range [0, VM_NUM_REGS), and every
 *      field the opcode does not use (dst, src, offset, imm) is zero, so
 *      each program has exactly one binary encoding (docs/ISA.md).
 *      Every RET returns VM_VERDICT_DROP (0) or VM_VERDICT_PASS (1).
 *   4. Every branch target lies strictly inside the program AND is a
 *      FORWARD-ONLY jump (target > current PC). This guarantees the
 *      control-flow graph is a DAG, so no cycle -- and therefore no
 *      infinite loop -- is representable in a program that passes
 *      verification (bounded execution, proven statically).
 *   5. The final instruction in the program must be VM_OP_RET, so the
 *      interpreter is guaranteed to reach an explicit exit instead of
 *      falling off the end of the instruction array.
 *
 * Packet-buffer memory bounds (offset vs. actual frame length) are
 * NOT checked here, because the frame length is only known at RUN
 * TIME (each packet has a different length). Those checks are done
 * by vm_interpreter_run() on every single packet load, with a
 * fail-safe DROP verdict on any out-of-bounds access.
 *
 * Returns VM_VERIFY_OK on success, otherwise the first error found.
 */
vm_verify_result_t vm_verify(const vm_program_t *prog);

const char *vm_verify_strerror(vm_verify_result_t r);

#ifdef __cplusplus
}
#endif

#endif /* VM_VERIFIER_H */
