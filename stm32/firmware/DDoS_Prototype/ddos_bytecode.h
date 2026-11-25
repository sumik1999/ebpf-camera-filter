#ifndef DDOS_BYTECODE_H
#define DDOS_BYTECODE_H

#include "vm_isa.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==================================================================
 * DDoS prototype bytecode.
 *
 * Drops any IPv4/UDP packet whose destination port equals
 * DDOS_TARGET_PORT (default 1900/SSDP -- one of the most commonly
 * abused ports for UDP reflection/amplification DDoS attacks; swap
 * in 53/DNS, 123/NTP, 19/CHARGEN etc. the same way). All other
 * traffic (non-IPv4, non-UDP, or any other port) is PASSed through
 * untouched. Both regressions R1 (VM) and R2 (hooks) meet here: this
 * is the exact byte-array Role 2 installs via xdp_hook_install_vm()
 * / lwip_ip4_hook_install_vm(). ddos_native.c is the same filter in C.
 *
 * TWO variants are provided because the two hook points in this
 * project see the frame at different offsets:
 *
 *   ddos_program_eth -> used by the DMA/ingress hook, which sees the
 *                        FULL Ethernet frame (L2 header included).
 *   ddos_program_ip  -> used by the lwIP ip4_input() hook, which
 *                        sees the frame starting at the IP header
 *                        (netif->input() has already stripped the
 *                        14-byte Ethernet header).
 *
 * Using two bytecode variants that implement identical filtering
 * logic (just at different fixed offsets) keeps the Role 2
 * benchmarking comparison apples-to-apples: the SAME VM, the SAME
 * decision logic, only the ingress point differs.
 *
 * Both variants assume no IPv4 options (IHL = 5, 20-byte header),
 * which holds for the synthetic UDP flood traffic used to exercise
 * this prototype; a production filter would first read the IHL
 * nibble and compute the UDP header offset dynamically.
 * ================================================================== */

#define DDOS_TARGET_PORT   1900u   /* SSDP - common UDP amplification vector */

/* ---------------------------------------------------------------
 * Variant A: full Ethernet frame (DMA/ingress hook, xdp_dma_hook.c)
 *
 * Layout assumed:
 *   [0..11]   dst MAC (6) + src MAC (6)
 *   [12..13]  EtherType            (0x0800 = IPv4)
 *   [14]      IP ver/IHL           (assumes IHL = 5 -> 20B header)
 *   [23]      IP protocol          (17 = UDP)
 *   [34..35]  UDP source port      (14 + 20)
 *   [36..37]  UDP destination port
 *
 * Program (R0 is always 0, used purely as the "packet base" index
 * register for every LDPKT instruction):
 *
 *   0: R1 = ldpkt_h [R0+12]                  ; EtherType
 *   1: if (R1 != 0x0800) goto PASS           ; not IPv4 -> pass
 *   2: R2 = ldpkt_b [R0+23]                  ; IP protocol
 *   3: if (R2 != 17) goto PASS               ; not UDP -> pass
 *   4: R3 = ldpkt_h [R0+36]                  ; UDP dest port
 *   5: if (R3 != DDOS_TARGET_PORT) goto PASS ; wrong port -> pass
 *   6: return DROP                           ; IPv4+UDP+target port
 *   7: PASS: return PASS
 * --------------------------------------------------------------- */
static const vm_insn_t ddos_bytecode_eth[] = {
    /* 0 */ { VM_OP_LDPKT_H, 1, 0, 12, 0 },
    /* 1 */ { VM_OP_JNE_K,   1, 0, 5,  0x0800 },
    /* 2 */ { VM_OP_LDPKT_B, 2, 0, 23, 0 },
    /* 3 */ { VM_OP_JNE_K,   2, 0, 3,  17 },
    /* 4 */ { VM_OP_LDPKT_H, 3, 0, 36, 0 },
    /* 5 */ { VM_OP_JNE_K,   3, 0, 1,  DDOS_TARGET_PORT },
    /* 6 */ { VM_OP_RET,     0, 0, 0,  VM_VERDICT_DROP },
    /* 7 */ { VM_OP_RET,     0, 0, 0,  VM_VERDICT_PASS },
};

static const vm_program_t ddos_program_eth = {
    .insns   = ddos_bytecode_eth,
    .n_insns = (uint16_t)(sizeof(ddos_bytecode_eth) / sizeof(ddos_bytecode_eth[0])),
    .ctx     = VM_CTX_ETH,
};

/* ---------------------------------------------------------------
 * Variant B: buffer starts at the IP header (lwIP ip4_input hook,
 * lwip_ip4_hook.c). Same logic, offsets shifted back by 14 bytes:
 *
 *   [9]       IP protocol          (17 = UDP)
 *   [20..21]  UDP source port      (20 + 0)
 *   [22..23]  UDP destination port
 * --------------------------------------------------------------- */
static const vm_insn_t ddos_bytecode_ip_offset[] = {
    /* 0 */ { VM_OP_LDPKT_B, 2, 0, 9,  0 },
    /* 1 */ { VM_OP_JNE_K,   2, 0, 3,  17 },
    /* 2 */ { VM_OP_LDPKT_H, 3, 0, 22, 0 },
    /* 3 */ { VM_OP_JNE_K,   3, 0, 1,  DDOS_TARGET_PORT },
    /* 4 */ { VM_OP_RET,     0, 0, 0,  VM_VERDICT_DROP },
    /* 5 */ { VM_OP_RET,     0, 0, 0,  VM_VERDICT_PASS },
};

static const vm_program_t ddos_program_ip = {
    .insns   = ddos_bytecode_ip_offset,
    .n_insns = (uint16_t)(sizeof(ddos_bytecode_ip_offset) / sizeof(ddos_bytecode_ip_offset[0])),
    .ctx     = VM_CTX_IPV4,
};

#ifdef __cplusplus
}
#endif

#endif /* DDOS_BYTECODE_H */
