#ifndef META_FILTER_BYTECODE_H
#define META_FILTER_BYTECODE_H

#include "vm_isa.h"
#include "packet_meta.h"
#include "packet_parser.h"
#include "vm_meta_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define META_POLICY_VLAN       100u
#define META_POLICY_UDP_PORT    53u
#define META_POLICY_TCP_PORT   443u
#define META_RATE_CLASS_DNS      0u
#define META_RATE_CLASS_HTTPS    1u

/* ISA-v2 equivalent of the strict native reference policy. It runs on the
 * canonical 32-byte metadata ABI, not on raw packet offsets. The service
 * helper is bounded native state; r1 selects DNS/HTTPS and r0 returns allow. */
static const vm_insn_t meta_filter_insns[] = {
    /*  0 */ { VM_OP_LDPKT_B, 1, 0, VM_META_OFF_PARSE_STATUS, 0 },
    /*  1 */ { VM_OP_JNE_K,   1, 0, 33, 0 },                       /* -> DROP 35 */
    /*  2 */ { VM_OP_LDPKT_B, 1, 0, VM_META_OFF_ABI_VERSION, 0 },
    /*  3 */ { VM_OP_JNE_K,   1, 0, 31, VM_META_ABI_VERSION },
    /*  4 */ { VM_OP_LDPKT_B, 1, 0, VM_META_OFF_FRAGMENT, 0 },
    /*  5 */ { VM_OP_JNE_K,   1, 0, 29, PACKET_FRAGMENT_NONE },
    /*  6 */ { VM_OP_LDPKT_B, 1, 0, VM_META_OFF_VLAN_DEPTH, 0 },
    /*  7 */ { VM_OP_JEQ_K,   1, 0, 3, 0 },                        /* -> L3 11 */
    /*  8 */ { VM_OP_LDPKT_H, 1, 0, VM_META_OFF_VLAN_ID, 0 },
    /*  9 */ { VM_OP_JEQ_K,   1, 0, 1, META_POLICY_VLAN },         /* -> L3 11 */
    /* 10 */ { VM_OP_JMP,     0, 0, 24, 0 },                       /* -> DROP */
    /* 11 */ { VM_OP_LDPKT_B, 1, 0, VM_META_OFF_L3_TYPE, 0 },
    /* 12 */ { VM_OP_JEQ_K,   1, 0, 23, PACKET_L3_ARP },           /* -> PASS 36 */
    /* 13 */ { VM_OP_JNE_K,   1, 0, 21, PACKET_L3_IPV4 },         /* -> DROP */
    /* 14 */ { VM_OP_LDPKT_B, 2, 0, VM_META_OFF_PROTOCOL, 0 },
    /* 15 */ { VM_OP_JEQ_K,   2, 0, 20, PACKET_IPPROTO_ICMP },     /* -> PASS */
    /* 16 */ { VM_OP_JEQ_K,   2, 0, 2, PACKET_IPPROTO_UDP },       /* -> UDP 19 */
    /* 17 */ { VM_OP_JEQ_K,   2, 0, 9, PACKET_IPPROTO_TCP },       /* -> TCP 27 */
    /* 18 */ { VM_OP_JMP,     0, 0, 16, 0 },                       /* -> DROP */

    /* UDP/DNS */
    /* 19 */ { VM_OP_LDPKT_B, 3, 0, VM_META_OFF_L4_PRESENT, 0 },
    /* 20 */ { VM_OP_JNE_K,   3, 0, 14, 1 },
    /* 21 */ { VM_OP_LDPKT_H, 3, 0, VM_META_OFF_DST_PORT, 0 },
    /* 22 */ { VM_OP_JNE_K,   3, 0, 12, META_POLICY_UDP_PORT },
    /* 23 */ { VM_OP_MOV_K,   1, 0, 0, META_RATE_CLASS_DNS },
    /* 24 */ { VM_OP_CALL,    0, 0, 0, VM_HELPER_RATE_SERVICE },
    /* 25 */ { VM_OP_JEQ_K,   0, 0, 9, 0 },                        /* denied -> DROP */
    /* 26 */ { VM_OP_JMP,     0, 0, 9, 0 },                        /* -> PASS */

    /* TCP/HTTPS */
    /* 27 */ { VM_OP_LDPKT_B, 3, 0, VM_META_OFF_L4_PRESENT, 0 },
    /* 28 */ { VM_OP_JNE_K,   3, 0, 6, 1 },
    /* 29 */ { VM_OP_LDPKT_H, 3, 0, VM_META_OFF_DST_PORT, 0 },
    /* 30 */ { VM_OP_JNE_K,   3, 0, 4, META_POLICY_TCP_PORT },
    /* 31 */ { VM_OP_MOV_K,   1, 0, 0, META_RATE_CLASS_HTTPS },
    /* 32 */ { VM_OP_CALL,    0, 0, 0, VM_HELPER_RATE_SERVICE },
    /* 33 */ { VM_OP_JEQ_K,   0, 0, 1, 0 },                        /* denied -> DROP */
    /* 34 */ { VM_OP_JMP,     0, 0, 1, 0 },                        /* -> PASS */

    /* 35 */ { VM_OP_RET,     0, 0, 0, VM_VERDICT_DROP },
    /* 36 */ { VM_OP_RET,     0, 0, 0, VM_VERDICT_PASS },
};

static const vm_program_t meta_filter_program = {
    .insns = meta_filter_insns,
    .n_insns = (uint16_t)(sizeof(meta_filter_insns) / sizeof(meta_filter_insns[0])),
    .ctx = VM_CTX_META,
};

#ifdef __cplusplus
}
#endif

#endif /* META_FILTER_BYTECODE_H */
