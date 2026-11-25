#include "vm_interpreter.h"

/* Fixed-size register file: VM_NUM_REGS x 32-bit. No heap, no VLA,
 * lives entirely on the caller's stack frame (small, fixed size --
 * safe to call from a task with a tight FreeRTOS stack budget). */
typedef struct {
    uint32_t r[VM_NUM_REGS];
} vm_regs_t;

static inline int pkt_bounds_ok(uint32_t pkt_len, uint32_t addr_off, uint32_t access_size)
{
    if (access_size > pkt_len) {
        return 0;
    }
    if (addr_off > (pkt_len - access_size)) {
        return 0;
    }
    return 1;
}

/* D11: every exit path goes through here, so insns_executed is always
 * filled in -- also when the run stops early on a fault. It counts the
 * instructions the VM started, including a load that faulted. */
static inline int vm_finish(vm_run_stats_t *out, vm_run_stats_t *st,
                            uint32_t executed, int verdict)
{
    st->insns_executed = executed;
    if (out) *out = *st;
    return verdict;
}

int vm_interpreter_run(const vm_program_t *prog,
                        const uint8_t *pkt_data,
                        uint32_t pkt_len,
                        vm_run_stats_t *stats)
{
    vm_run_stats_t local_stats = {0, 0, 0};
    vm_regs_t regs;
    /* ISA: all registers start at 0. Written as 8 explicit stores because
     * `regs = {{0}}` made GCC call memset(), and newlib-nano's memset copies
     * byte by byte: 133 of the 242 instructions of the DDoS ARP path on the
     * Cortex-M33 (QEMU count, eval/raw/2026-11-24_96f85bd_qemu_insns-O2). */
    _Static_assert(VM_NUM_REGS == 8u, "update the register initialisation");
    regs.r[0] = 0; regs.r[1] = 0; regs.r[2] = 0; regs.r[3] = 0;
    regs.r[4] = 0; regs.r[5] = 0; regs.r[6] = 0; regs.r[7] = 0;
    uint16_t pc = 0;
    uint32_t steps = 0;

    if (prog == NULL || prog->insns == NULL || prog->n_insns == 0 || pkt_data == NULL) {
        return vm_finish(stats, &local_stats, 0, VM_VERDICT_DROP); /* fail safe on malformed input */
    }

    for (;;) {
        if (pc >= prog->n_insns) {
            /* Unreachable for a verified program (must end in RET),
             * kept as a defense-in-depth guard -> fail safe. */
            local_stats.aborted_oob++;
            return vm_finish(stats, &local_stats, steps, VM_VERDICT_DROP);
        }
        if (++steps > VM_MAX_STEPS) {
            local_stats.aborted_budget++;
            /* the (VM_MAX_STEPS+1)-th instruction is never started */
            return vm_finish(stats, &local_stats, VM_MAX_STEPS, VM_VERDICT_DROP);
        }

        const vm_insn_t ins = prog->insns[pc];
        uint16_t next_pc = (uint16_t)(pc + 1);

        switch (ins.opcode) {

        /* ---------------- packet loads (bounds-checked every time) ---- */
        case VM_OP_LDPKT_B: {
            uint32_t off = regs.r[ins.src_reg] + (int32_t)ins.offset;
            if (!pkt_bounds_ok(pkt_len, off, 1u)) {
                local_stats.aborted_oob++;
                return vm_finish(stats, &local_stats, steps, VM_VERDICT_DROP);
            }
            regs.r[ins.dst_reg] = pkt_data[off];
            break;
        }
        case VM_OP_LDPKT_H: {
            uint32_t off = regs.r[ins.src_reg] + (int32_t)ins.offset;
            if (!pkt_bounds_ok(pkt_len, off, 2u)) {
                local_stats.aborted_oob++;
                return vm_finish(stats, &local_stats, steps, VM_VERDICT_DROP);
            }
            regs.r[ins.dst_reg] = ((uint32_t)pkt_data[off] << 8) |
                                    (uint32_t)pkt_data[off + 1];
            break;
        }
        case VM_OP_LDPKT_W: {
            uint32_t off = regs.r[ins.src_reg] + (int32_t)ins.offset;
            if (!pkt_bounds_ok(pkt_len, off, 4u)) {
                local_stats.aborted_oob++;
                return vm_finish(stats, &local_stats, steps, VM_VERDICT_DROP);
            }
            regs.r[ins.dst_reg] = ((uint32_t)pkt_data[off]     << 24) |
                                   ((uint32_t)pkt_data[off + 1] << 16) |
                                   ((uint32_t)pkt_data[off + 2] << 8)  |
                                    (uint32_t)pkt_data[off + 3];
            break;
        }

        /* ---------------- ALU, immediate operand ----------------------- */
        case VM_OP_ADD_K: regs.r[ins.dst_reg] += (uint32_t)ins.imm; break;
        case VM_OP_SUB_K: regs.r[ins.dst_reg] -= (uint32_t)ins.imm; break;
        case VM_OP_AND_K: regs.r[ins.dst_reg] &= (uint32_t)ins.imm; break;
        case VM_OP_OR_K:  regs.r[ins.dst_reg] |= (uint32_t)ins.imm; break;
        case VM_OP_XOR_K: regs.r[ins.dst_reg] ^= (uint32_t)ins.imm; break;
        case VM_OP_LSH_K: regs.r[ins.dst_reg] <<= ((uint32_t)ins.imm & 31u); break;
        case VM_OP_RSH_K: regs.r[ins.dst_reg] >>= ((uint32_t)ins.imm & 31u); break;

        /* ---------------- ALU, register operand ------------------------ */
        case VM_OP_ADD_X: regs.r[ins.dst_reg] += regs.r[ins.src_reg]; break;
        case VM_OP_SUB_X: regs.r[ins.dst_reg] -= regs.r[ins.src_reg]; break;
        case VM_OP_AND_X: regs.r[ins.dst_reg] &= regs.r[ins.src_reg]; break;
        case VM_OP_OR_X:  regs.r[ins.dst_reg] |= regs.r[ins.src_reg]; break;
        case VM_OP_XOR_X: regs.r[ins.dst_reg] ^= regs.r[ins.src_reg]; break;
        case VM_OP_LSH_X: regs.r[ins.dst_reg] <<= (regs.r[ins.src_reg] & 31u); break;
        case VM_OP_RSH_X: regs.r[ins.dst_reg] >>= (regs.r[ins.src_reg] & 31u); break;

        /* ---------------- mov ------------------------------------------- */
        case VM_OP_MOV_K: regs.r[ins.dst_reg] = (uint32_t)ins.imm;   break;
        case VM_OP_MOV_X: regs.r[ins.dst_reg] = regs.r[ins.src_reg]; break;

        /* ---------------- branches (forward-only, verified) ------------- */
        case VM_OP_JMP:
            next_pc = (uint16_t)(pc + 1 + ins.offset);
            break;

        case VM_OP_JEQ_K: if (regs.r[ins.dst_reg] == (uint32_t)ins.imm) next_pc = (uint16_t)(pc + 1 + ins.offset); break;
        case VM_OP_JNE_K: if (regs.r[ins.dst_reg] != (uint32_t)ins.imm) next_pc = (uint16_t)(pc + 1 + ins.offset); break;
        case VM_OP_JGT_K: if (regs.r[ins.dst_reg] >  (uint32_t)ins.imm) next_pc = (uint16_t)(pc + 1 + ins.offset); break;
        case VM_OP_JGE_K: if (regs.r[ins.dst_reg] >= (uint32_t)ins.imm) next_pc = (uint16_t)(pc + 1 + ins.offset); break;
        case VM_OP_JLT_K: if (regs.r[ins.dst_reg] <  (uint32_t)ins.imm) next_pc = (uint16_t)(pc + 1 + ins.offset); break;
        case VM_OP_JLE_K: if (regs.r[ins.dst_reg] <= (uint32_t)ins.imm) next_pc = (uint16_t)(pc + 1 + ins.offset); break;

        case VM_OP_JEQ_X: if (regs.r[ins.dst_reg] == regs.r[ins.src_reg]) next_pc = (uint16_t)(pc + 1 + ins.offset); break;
        case VM_OP_JNE_X: if (regs.r[ins.dst_reg] != regs.r[ins.src_reg]) next_pc = (uint16_t)(pc + 1 + ins.offset); break;
        case VM_OP_JGT_X: if (regs.r[ins.dst_reg] >  regs.r[ins.src_reg]) next_pc = (uint16_t)(pc + 1 + ins.offset); break;
        case VM_OP_JGE_X: if (regs.r[ins.dst_reg] >= regs.r[ins.src_reg]) next_pc = (uint16_t)(pc + 1 + ins.offset); break;
        case VM_OP_JLT_X: if (regs.r[ins.dst_reg] <  regs.r[ins.src_reg]) next_pc = (uint16_t)(pc + 1 + ins.offset); break;
        case VM_OP_JLE_X: if (regs.r[ins.dst_reg] <= regs.r[ins.src_reg]) next_pc = (uint16_t)(pc + 1 + ins.offset); break;

        /* ---------------- exit ------------------------------------------- */
        case VM_OP_RET:
            return vm_finish(stats, &local_stats, steps,
                             (ins.imm == VM_VERDICT_PASS) ? VM_VERDICT_PASS : VM_VERDICT_DROP);

        default:
            /* Unreachable for a program that passed vm_verify(). */
            local_stats.aborted_oob++;
            return vm_finish(stats, &local_stats, steps, VM_VERDICT_DROP);
        }

        pc = next_pc;
    }
}
