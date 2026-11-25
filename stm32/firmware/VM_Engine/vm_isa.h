#ifndef VM_ISA_H
#define VM_ISA_H

#include <stdint.h>
#include <stddef.h>   /* NULL, size_t -- v0.1 fix: was missing, broke compilation */

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * Lightweight eBPF-like Instruction Set Architecture (ISA)
 * for the packet-filtering bytecode VM running on STM32
 * (no heap, no dynamic allocation anywhere in this module).
 * ============================================================
 *
 * Instruction encoding: fixed-width 64-bit, layout inspired by
 * classic eBPF but deliberately simplified so the verifier stays
 * small and fully auditable for an MTech-scope project.
 *
 *   MSB                                                   LSB
 *   +--------+--------+--------+----------------+--------------+
 *   | opcode | dst:4  | src:4  |   offset (16)  |   imm (32)   |
 *   +--------+--------+--------+----------------+--------------+
 *      8 bit    4 bit   4 bit        16 bit          32 bit
 *
 *   8 + 4 + 4 + 16 + 32 = 64 bits total.
 */

typedef struct __attribute__((packed)) {
    uint8_t  opcode;       /* operation                          */
    uint8_t  dst_reg : 4;  /* destination register (0..VM_NUM_REGS-1) */
    uint8_t  src_reg : 4;  /* source register / base register    */
    int16_t  offset;       /* signed offset: branch delta OR packet mem offset */
    int32_t  imm;          /* signed 32-bit immediate             */
} vm_insn_t;

_Static_assert(sizeof(vm_insn_t) == 8, "vm_insn_t must be exactly 64 bits wide");

/* ISA version (docs/ISA.md). v1 frozen 28 Oct 2026: any change to the
 * encoding or semantics is a new version, with ISA.md, code and tests
 * changed in the same commit. */
#define VM_ISA_VERSION   1u

#define VM_NUM_REGS      8u
#define VM_MAX_INSNS     512u    /* hard cap enforced by the verifier   */
#define VM_MAX_STEPS     2000u   /* runtime step budget (interpreter)   */

/* ---------------- Opcode classes (low 3 bits of opcode) ------ */
#define VM_CLASS_MASK    0x07u
#define VM_CLASS_LD      0x00u   /* packet load                */
#define VM_CLASS_ALU_K   0x01u   /* alu,   src operand = imm   */
#define VM_CLASS_ALU_X   0x02u   /* alu,   src operand = reg   */
#define VM_CLASS_JMP_K   0x03u   /* branch,src operand = imm   */
#define VM_CLASS_JMP_X   0x04u   /* branch,src operand = reg   */
#define VM_CLASS_RET     0x05u   /* return / exit              */
#define VM_CLASS_MISC    0x06u   /* mov / load-immediate       */
#define VM_CLASS_RESERVED 0x07u  /* reserved for ISA v2 extensions (e.g. a stateful
                                    counter map, a scratch stack); the verifier
                                    rejects every opcode in this class          */

/* ---------------- Full opcodes = (op_within_class << 3) | class --- */

/* Packet loads: dst_reg = *(size *)(pkt_data + reg[src_reg] + offset) */
#define VM_OP_LDPKT_B    ((0u << 3) | VM_CLASS_LD)   /* 1 byte  */
#define VM_OP_LDPKT_H    ((1u << 3) | VM_CLASS_LD)   /* 2 bytes, network byte order */
#define VM_OP_LDPKT_W    ((2u << 3) | VM_CLASS_LD)   /* 4 bytes, network byte order */

/* ALU sub-operations */
#define VM_ALU_ADD  0u
#define VM_ALU_SUB  1u
#define VM_ALU_AND  2u
#define VM_ALU_OR   3u
#define VM_ALU_XOR  4u
#define VM_ALU_LSH  5u
#define VM_ALU_RSH  6u

#define VM_OP_ADD_K  ((VM_ALU_ADD << 3) | VM_CLASS_ALU_K)
#define VM_OP_SUB_K  ((VM_ALU_SUB << 3) | VM_CLASS_ALU_K)
#define VM_OP_AND_K  ((VM_ALU_AND << 3) | VM_CLASS_ALU_K)
#define VM_OP_OR_K   ((VM_ALU_OR  << 3) | VM_CLASS_ALU_K)
#define VM_OP_XOR_K  ((VM_ALU_XOR << 3) | VM_CLASS_ALU_K)
#define VM_OP_LSH_K  ((VM_ALU_LSH << 3) | VM_CLASS_ALU_K)
#define VM_OP_RSH_K  ((VM_ALU_RSH << 3) | VM_CLASS_ALU_K)

#define VM_OP_ADD_X  ((VM_ALU_ADD << 3) | VM_CLASS_ALU_X)
#define VM_OP_SUB_X  ((VM_ALU_SUB << 3) | VM_CLASS_ALU_X)
#define VM_OP_AND_X  ((VM_ALU_AND << 3) | VM_CLASS_ALU_X)
#define VM_OP_OR_X   ((VM_ALU_OR  << 3) | VM_CLASS_ALU_X)
#define VM_OP_XOR_X  ((VM_ALU_XOR << 3) | VM_CLASS_ALU_X)
#define VM_OP_LSH_X  ((VM_ALU_LSH << 3) | VM_CLASS_ALU_X)
#define VM_OP_RSH_X  ((VM_ALU_RSH << 3) | VM_CLASS_ALU_X)

/* Branch sub-operations: PC_next = PC + 1 + offset  IF  (condition) */
#define VM_JMP_JMP  0u   /* unconditional */
#define VM_JMP_JEQ  1u
#define VM_JMP_JNE  2u
#define VM_JMP_JGT  3u
#define VM_JMP_JGE  4u
#define VM_JMP_JLT  5u
#define VM_JMP_JLE  6u

#define VM_OP_JMP    ((VM_JMP_JMP << 3) | VM_CLASS_JMP_K)  /* unconditional, offset only */
#define VM_OP_JEQ_K  ((VM_JMP_JEQ << 3) | VM_CLASS_JMP_K)
#define VM_OP_JNE_K  ((VM_JMP_JNE << 3) | VM_CLASS_JMP_K)
#define VM_OP_JGT_K  ((VM_JMP_JGT << 3) | VM_CLASS_JMP_K)
#define VM_OP_JGE_K  ((VM_JMP_JGE << 3) | VM_CLASS_JMP_K)
#define VM_OP_JLT_K  ((VM_JMP_JLT << 3) | VM_CLASS_JMP_K)
#define VM_OP_JLE_K  ((VM_JMP_JLE << 3) | VM_CLASS_JMP_K)

#define VM_OP_JEQ_X  ((VM_JMP_JEQ << 3) | VM_CLASS_JMP_X)
#define VM_OP_JNE_X  ((VM_JMP_JNE << 3) | VM_CLASS_JMP_X)
#define VM_OP_JGT_X  ((VM_JMP_JGT << 3) | VM_CLASS_JMP_X)
#define VM_OP_JGE_X  ((VM_JMP_JGE << 3) | VM_CLASS_JMP_X)
#define VM_OP_JLT_X  ((VM_JMP_JLT << 3) | VM_CLASS_JMP_X)
#define VM_OP_JLE_X  ((VM_JMP_JLE << 3) | VM_CLASS_JMP_X)

/* Misc */
#define VM_OP_MOV_K  ((0u << 3) | VM_CLASS_MISC)  /* dst = imm  */
#define VM_OP_MOV_X  ((1u << 3) | VM_CLASS_MISC)  /* dst = src  */

/* Return / exit: verdict = imm (0 = DROP, 1 = PASS). Ends the program. */
#define VM_OP_RET    ((0u << 3) | VM_CLASS_RET)

/* ---------------- Verdicts ------------------------------------ */
#define VM_VERDICT_DROP   0
#define VM_VERDICT_PASS   1

/* ---------------- Program context (ISA v1) ---------------------
 * What pkt[0] is when the program runs. A program declares the one
 * context it was written for; the verifier rejects unknown values
 * and the install API rejects a program meant for another hook.
 * 0 is deliberately invalid so a forgotten field is caught.        */
#define VM_CTX_ETH    1u   /* Ethernet frame: pkt[0] = destination MAC (driver hook)  */
#define VM_CTX_IPV4   2u   /* IPv4 packet:    pkt[0] = IPv4 header     (lwIP hook)    */

/* ---------------- VM program container ------------------------- */
typedef struct {
    const vm_insn_t *insns;
    uint16_t         n_insns;
    uint8_t          ctx;      /* VM_CTX_ETH or VM_CTX_IPV4 */
} vm_program_t;

#ifdef __cplusplus
}
#endif

#endif /* VM_ISA_H */
