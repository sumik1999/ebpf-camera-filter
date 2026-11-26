#include "vm_verifier.h"

static int is_reg_ok(uint8_t r)
{
    return r < VM_NUM_REGS;
}

static int opcode_is_known(uint8_t op)
{
    switch (op) {
    case VM_OP_LDPKT_B: case VM_OP_LDPKT_H: case VM_OP_LDPKT_W:

    case VM_OP_ADD_K: case VM_OP_SUB_K: case VM_OP_AND_K:
    case VM_OP_OR_K:  case VM_OP_XOR_K: case VM_OP_LSH_K: case VM_OP_RSH_K:

    case VM_OP_ADD_X: case VM_OP_SUB_X: case VM_OP_AND_X:
    case VM_OP_OR_X:  case VM_OP_XOR_X: case VM_OP_LSH_X: case VM_OP_RSH_X:

    case VM_OP_JMP:
    case VM_OP_JEQ_K: case VM_OP_JNE_K: case VM_OP_JGT_K:
    case VM_OP_JGE_K: case VM_OP_JLT_K: case VM_OP_JLE_K:

    case VM_OP_JEQ_X: case VM_OP_JNE_X: case VM_OP_JGT_X:
    case VM_OP_JGE_X: case VM_OP_JLT_X: case VM_OP_JLE_X:

    case VM_OP_MOV_K: case VM_OP_MOV_X:
    case VM_OP_RET: case VM_OP_CALL:
        return 1;
    default:
        return 0;
    }
}

static int is_branch_opcode(uint8_t op)
{
    uint8_t cls = op & VM_CLASS_MASK;
    return (cls == VM_CLASS_JMP_K || cls == VM_CLASS_JMP_X);
}

/* Fields each opcode reads (docs/ISA.md section 4). Only called for known opcodes. */
#define FIELD_DST 1u
#define FIELD_SRC 2u
#define FIELD_OFF 4u
#define FIELD_IMM 8u

static unsigned used_fields(uint8_t op)
{
    switch (op & VM_CLASS_MASK) {
    case VM_CLASS_LD:    return FIELD_DST | FIELD_SRC | FIELD_OFF;
    case VM_CLASS_ALU_K: return FIELD_DST | FIELD_IMM;
    case VM_CLASS_ALU_X: return FIELD_DST | FIELD_SRC;
    case VM_CLASS_JMP_K: return (op == VM_OP_JMP) ? FIELD_OFF : (FIELD_DST | FIELD_OFF | FIELD_IMM);
    case VM_CLASS_JMP_X: return FIELD_DST | FIELD_SRC | FIELD_OFF;
    case VM_CLASS_MISC:  return (op == VM_OP_MOV_K) ? (FIELD_DST | FIELD_IMM) : (FIELD_DST | FIELD_SRC);
    case VM_CLASS_RET:   return FIELD_IMM;
    default:             return FIELD_IMM;                       /* VM_CLASS_CALL */
    }
}

/* Canonical encoding: every field the opcode does not use must be zero. */
static int unused_fields_are_zero(const vm_insn_t *ins)
{
    unsigned used = used_fields(ins->opcode);
    if (!(used & FIELD_DST) && ins->dst_reg != 0) return 0;
    if (!(used & FIELD_SRC) && ins->src_reg != 0) return 0;
    if (!(used & FIELD_OFF) && ins->offset  != 0) return 0;
    if (!(used & FIELD_IMM) && ins->imm     != 0) return 0;
    return 1;
}

vm_verify_result_t vm_verify(const vm_program_t *prog)
{
    if (prog == NULL || prog->insns == NULL || prog->n_insns == 0) {
        return VM_VERIFY_ERR_EMPTY;
    }
    if (prog->n_insns > VM_MAX_INSNS) {
        return VM_VERIFY_ERR_TOO_LONG;
    }
    if (prog->ctx != VM_CTX_ETH && prog->ctx != VM_CTX_IPV4 && prog->ctx != VM_CTX_META) {
        return VM_VERIFY_ERR_BAD_CTX;
    }

    const uint16_t n = prog->n_insns;

    for (uint16_t pc = 0; pc < n; pc++) {
        const vm_insn_t *ins = &prog->insns[pc];

        if (!opcode_is_known(ins->opcode)) {
            return VM_VERIFY_ERR_BAD_OPCODE;
        }
        if (!is_reg_ok(ins->dst_reg) || !is_reg_ok(ins->src_reg)) {
            return VM_VERIFY_ERR_BAD_REG;
        }
        if (!unused_fields_are_zero(ins)) {
            return VM_VERIFY_ERR_UNUSED_FIELD;
        }
        if (ins->opcode == VM_OP_RET &&
            ins->imm != VM_VERDICT_DROP && ins->imm != VM_VERDICT_PASS) {
            return VM_VERIFY_ERR_BAD_RET;
        }
        if (ins->opcode == VM_OP_CALL) {
            uint32_t helper = (uint32_t)ins->imm;
            if (prog->ctx != VM_CTX_META || helper >= VM_HELPER_COUNT) {
                return VM_VERIFY_ERR_BAD_HELPER;
            }
        }

        if (is_branch_opcode(ins->opcode)) {
            /* eBPF-style relative branch: target = pc + 1 + offset */
            int32_t target = (int32_t)pc + 1 + (int32_t)ins->offset;

            if (target <= (int32_t)pc) {
                /* Disallow backward / self jumps. This is what makes
                 * "bounded execution" provable statically: the CFG of
                 * any program that passes this check is acyclic, so
                 * no representable program can loop forever. */
                return VM_VERIFY_ERR_BACKWARD_JUMP;
            }
            if (target >= (int32_t)n) {
                return VM_VERIFY_ERR_BAD_JUMP;
            }
        }
    }

    /* The program must terminate in an explicit RET as its last
     * instruction, guaranteeing the interpreter always reaches a
     * defined exit point. */
    if (prog->insns[n - 1].opcode != VM_OP_RET) {
        return VM_VERIFY_ERR_NO_RET;
    }

    return VM_VERIFY_OK;
}

uint32_t vm_program_required_helpers(const vm_program_t *prog)
{
    uint32_t mask = 0u;
    if (prog == NULL || prog->insns == NULL) return 0u;
    for (uint16_t i = 0; i < prog->n_insns; i++) {
        if (prog->insns[i].opcode == VM_OP_CALL &&
            (uint32_t)prog->insns[i].imm < VM_HELPER_COUNT) {
            mask |= VM_HELPER_BIT((uint32_t)prog->insns[i].imm);
        }
    }
    return mask;
}

const char *vm_verify_strerror(vm_verify_result_t r)
{
    switch (r) {
    case VM_VERIFY_OK:                return "OK";
    case VM_VERIFY_ERR_EMPTY:         return "empty program";
    case VM_VERIFY_ERR_TOO_LONG:      return "program exceeds VM_MAX_INSNS";
    case VM_VERIFY_ERR_BAD_OPCODE:    return "unknown opcode";
    case VM_VERIFY_ERR_BAD_REG:       return "register index out of range";
    case VM_VERIFY_ERR_BAD_JUMP:      return "jump target out of program bounds";
    case VM_VERIFY_ERR_BACKWARD_JUMP: return "backward/self jump not allowed (would permit loops)";
    case VM_VERIFY_ERR_NO_RET:        return "program does not end in RET";
    case VM_VERIFY_ERR_UNUSED_FIELD:  return "field not used by this opcode is non-zero";
    case VM_VERIFY_ERR_BAD_RET:       return "RET value must be 0 (DROP) or 1 (PASS)";
    case VM_VERIFY_ERR_BAD_CTX:       return "unknown program context";
    case VM_VERIFY_ERR_BAD_VERSION:   return "unsupported ISA/context version";
    case VM_VERIFY_ERR_BAD_HELPER:    return "helper is unknown or not permitted";
    default:                          return "unknown verification error";
    }
}
