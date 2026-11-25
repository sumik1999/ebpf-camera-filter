/*
 * test_vm.c -- host-side unit tests for the VM engine + DDoS bytecode.
 *
 * Runs on a PC (gcc/clang), no STM32 hardware needed. This is the
 * fastest way to catch VM/verifier regressions before flashing.
 *
 *   make -C tests/host        (or: gcc -std=gnu11 -Wall -Wextra -O2 ...)
 *
 * Exit code 0 = all tests passed.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "vm_isa.h"
#include "vm_verifier.h"
#include "vm_interpreter.h"
#include "vm_slot.h"
#include "ddos_bytecode.h"

#include "test_common.h"

int g_fail = 0, g_pass = 0;

static int run_eth(const uint8_t *f, uint32_t len, vm_run_stats_t *st)
{
    return vm_interpreter_run(&ddos_program_eth, f, len, st);
}
static int run_ip(const uint8_t *f, uint32_t len)
{
    /* lwIP hook sees the frame starting at the IP header */
    return (len > ETH_HDR) ? vm_interpreter_run(&ddos_program_ip, f + ETH_HDR, len - ETH_HDR, NULL)
                           : VM_VERDICT_DROP;
}

/* ------------------------------------------------------------------ */
static void test_isa_layout(void)
{
    printf("[ISA] instruction layout\n");
    CHECK(sizeof(vm_insn_t) == 8, "vm_insn_t must be 8 bytes");
}

static void test_verifier(void)
{
    printf("[VERIFIER] accept / reject\n");
    CHECK(vm_verify(&ddos_program_eth) == VM_VERIFY_OK, "ddos_program_eth verifies");
    CHECK(vm_verify(&ddos_program_ip)  == VM_VERIFY_OK, "ddos_program_ip verifies");

    vm_program_t empty = { NULL, 0, VM_CTX_ETH };
    CHECK(vm_verify(&empty) == VM_VERIFY_ERR_EMPTY, "empty program rejected");
    CHECK(vm_verify(NULL)   == VM_VERIFY_ERR_EMPTY, "NULL program rejected");

    static const vm_insn_t back[] = {
        { VM_OP_MOV_K, 1, 0, 0, 0 },
        { VM_OP_JEQ_K, 1, 0, -2, 0 },               /* target = 1+1-2 = 0 (backward) */
        { VM_OP_RET,   0, 0, 0, VM_VERDICT_PASS },
    };
    vm_program_t pb = { back, 3, VM_CTX_ETH };
    CHECK(vm_verify(&pb) == VM_VERIFY_ERR_BACKWARD_JUMP, "backward jump rejected");

    static const vm_insn_t self[] = {
        { VM_OP_JMP, 0, 0, -1, 0 },                 /* target = 0 (self loop) */
        { VM_OP_RET, 0, 0, 0, VM_VERDICT_PASS },
    };
    vm_program_t ps = { self, 2, VM_CTX_ETH };
    CHECK(vm_verify(&ps) == VM_VERIFY_ERR_BACKWARD_JUMP, "self-loop rejected");

    static const vm_insn_t oob[] = {
        { VM_OP_JMP, 0, 0, 5, 0 },                  /* target = 6, program has 2 */
        { VM_OP_RET, 0, 0, 0, VM_VERDICT_PASS },
    };
    vm_program_t po = { oob, 2, VM_CTX_ETH };
    CHECK(vm_verify(&po) == VM_VERIFY_ERR_BAD_JUMP, "out-of-range jump rejected");

    static const vm_insn_t badop[] = {
        { 0xFF, 0, 0, 0, 0 },
        { VM_OP_RET, 0, 0, 0, VM_VERDICT_PASS },
    };
    vm_program_t pbo = { badop, 2, VM_CTX_ETH };
    CHECK(vm_verify(&pbo) == VM_VERIFY_ERR_BAD_OPCODE, "unknown opcode rejected");

    static const vm_insn_t badreg[] = {
        { VM_OP_MOV_K, 9, 0, 0, 0 },                /* r9 does not exist (8 regs) */
        { VM_OP_RET,   0, 0, 0, VM_VERDICT_PASS },
    };
    vm_program_t pbr = { badreg, 2, VM_CTX_ETH };
    CHECK(vm_verify(&pbr) == VM_VERIFY_ERR_BAD_REG, "register >= 8 rejected");

    static const vm_insn_t noret[] = {
        { VM_OP_MOV_K, 1, 0, 0, 0 },
    };
    vm_program_t pnr = { noret, 1, VM_CTX_ETH };
    CHECK(vm_verify(&pnr) == VM_VERIFY_ERR_NO_RET, "missing final RET rejected");

    vm_program_t ptl = { ddos_bytecode_eth, (uint16_t)(VM_MAX_INSNS + 1), VM_CTX_ETH };
    CHECK(vm_verify(&ptl) == VM_VERIFY_ERR_TOO_LONG, "over-length program rejected");
}

static void test_ddos_filter(void)
{
    uint8_t f[256];
    uint32_t n;
    vm_run_stats_t st;

    printf("[DDoS] verdicts on synthetic traffic (target UDP port %u)\n", DDOS_TARGET_PORT);

    frame_spec_t udp_attack = { 0x0800, 5, 17, 0x4000, 40000, DDOS_TARGET_PORT, 32 };
    n = build_frame(f, sizeof f, &udp_attack);
    CHECK(run_eth(f, n, &st) == VM_VERDICT_DROP, "UDP -> target port DROPPED (DMA variant)");
    printf("  insns executed on DROP path: %u\n", (unsigned)st.insns_executed);
    CHECK(run_ip(f, n) == VM_VERDICT_DROP, "UDP -> target port DROPPED (lwIP variant)");

    frame_spec_t udp_dns = { 0x0800, 5, 17, 0x4000, 40000, 53, 32 };
    n = build_frame(f, sizeof f, &udp_dns);
    CHECK(run_eth(f, n, NULL) == VM_VERDICT_PASS, "UDP -> port 53 PASSED (DMA)");
    CHECK(run_ip(f, n) == VM_VERDICT_PASS, "UDP -> port 53 PASSED (lwIP)");

    frame_spec_t tcp_same_port = { 0x0800, 5, 6, 0x4000, 40000, DDOS_TARGET_PORT, 32 };
    n = build_frame(f, sizeof f, &tcp_same_port);
    CHECK(run_eth(f, n, NULL) == VM_VERDICT_PASS, "TCP -> target port PASSED (filter is UDP-only)");

    frame_spec_t arp = { 0x0806, 0, 0, 0, 0, 0, 0 };
    n = build_frame(f, sizeof f, &arp);
    CHECK(run_eth(f, n, &st) == VM_VERDICT_PASS, "ARP PASSED");
    printf("  insns executed on non-IPv4 PASS path: %u\n", (unsigned)st.insns_executed);

    frame_spec_t ipv6 = { 0x86DD, 0, 0, 0, 0, 0, 0 };
    n = build_frame(f, sizeof f, &ipv6);
    CHECK(run_eth(f, n, NULL) == VM_VERDICT_PASS, "IPv6 PASSED");

    /* Runt / truncated frames: out-of-bounds load must fail SAFE (DROP). */
    n = build_frame(f, sizeof f, &udp_attack);
    CHECK(run_eth(f, 10, NULL) == VM_VERDICT_DROP, "10-byte runt frame -> DROP (OOB fail-safe)");
    CHECK(run_eth(f, 36, NULL) == VM_VERDICT_DROP, "frame cut before UDP dport -> DROP (OOB fail-safe)");
    CHECK(run_eth(f, 38, NULL) == VM_VERDICT_DROP, "frame ending exactly after dport -> reads OK, DROP by rule");

    /* Null inputs fail safe */
    CHECK(vm_interpreter_run(&ddos_program_eth, NULL, 64, NULL) == VM_VERDICT_DROP, "NULL packet -> DROP");
    CHECK(vm_interpreter_run(NULL, f, n, NULL) == VM_VERDICT_DROP, "NULL program -> DROP");
}

/* Documented limitations of the v0 prototype filter. These assert the
 * CURRENT behaviour so a future fix shows up as an intentional change. */
static void test_known_limitations(void)
{
    uint8_t f[256];
    uint32_t n;
    printf("[KNOWN LIMITATIONS] (expected behaviour of v0 bytecode)\n");

    /* IPv4 options shift the UDP header: fixed-offset filter misses it. */
    frame_spec_t opts = { 0x0800, 6, 17, 0x4000, 40000, DDOS_TARGET_PORT, 32 };
    n = build_frame(f, sizeof f, &opts);
    int v = run_eth(f, n, NULL);
    CHECK(v == VM_VERDICT_PASS, "IHL=6 attack frame currently PASSES (needs IHL-aware bytecode)");

    /* 802.1Q VLAN tag shifts everything by 4 bytes: not parsed. */
    frame_spec_t vlan = { 0x8100, 0, 0, 0, 0, 0, 0 };
    n = build_frame(f, sizeof f, &vlan);
    CHECK(run_eth(f, n, NULL) == VM_VERDICT_PASS, "VLAN-tagged frames currently PASS unfiltered");
}

static void test_alu_and_branches(void)
{
    printf("[ALU/JMP] instruction semantics\n");
    static const uint8_t dummy[4] = { 0xDE, 0xAD, 0xBE, 0xEF };

    /* r1 = 0xDEADBEEF via LDPKT_W; r1 >>= 16 -> 0xDEAD; r1 &= 0xFF -> 0xAD; == 0xAD ? PASS : DROP */
    static const vm_insn_t p1[] = {
        { VM_OP_LDPKT_W, 1, 0, 0, 0 },
        { VM_OP_RSH_K,   1, 0, 0, 16 },
        { VM_OP_AND_K,   1, 0, 0, 0xFF },
        { VM_OP_JEQ_K,   1, 0, 1, 0xAD },
        { VM_OP_RET,     0, 0, 0, VM_VERDICT_DROP },
        { VM_OP_RET,     0, 0, 0, VM_VERDICT_PASS },
    };
    vm_program_t prog1 = { p1, 6, VM_CTX_ETH };
    CHECK(vm_verify(&prog1) == VM_VERIFY_OK, "ALU test program verifies");
    CHECK(vm_interpreter_run(&prog1, dummy, 4, NULL) == VM_VERDICT_PASS, "LDPKT_W + RSH + AND + JEQ");

    /* register-register compare and MOV_X */
    static const vm_insn_t p2[] = {
        { VM_OP_MOV_K, 1, 0, 0, 7 },
        { VM_OP_MOV_X, 2, 1, 0, 0 },
        { VM_OP_ADD_K, 2, 0, 0, 1 },     /* r2 = 8 */
        { VM_OP_JGT_X, 2, 1, 1, 0 },     /* r2 > r1 -> PASS */
        { VM_OP_RET,   0, 0, 0, VM_VERDICT_DROP },
        { VM_OP_RET,   0, 0, 0, VM_VERDICT_PASS },
    };
    vm_program_t prog2 = { p2, 6, VM_CTX_ETH };
    CHECK(vm_verify(&prog2) == VM_VERIFY_OK, "reg-reg program verifies");
    CHECK(vm_interpreter_run(&prog2, dummy, 4, NULL) == VM_VERDICT_PASS, "MOV_X + ADD_K + JGT_X");

    /* negative packet offset via register base must be caught at run time */
    static const vm_insn_t p3[] = {
        { VM_OP_LDPKT_B, 1, 0, -1, 0 },  /* offset -1 -> wraps to 0xFFFFFFFF */
        { VM_OP_RET,     0, 0, 0, VM_VERDICT_PASS },
    };
    vm_program_t prog3 = { p3, 2, VM_CTX_ETH };
    CHECK(vm_verify(&prog3) == VM_VERIFY_OK, "negative-offset load passes STATIC verify (len unknown)");
    CHECK(vm_interpreter_run(&prog3, dummy, 4, NULL) == VM_VERDICT_DROP, "negative offset caught at RUNTIME -> DROP");

    /* RET with an imm other than 0/1 is treated as DROP */
    static const vm_insn_t p4[] = { { VM_OP_RET, 0, 0, 0, 42 } };
    vm_program_t prog4 = { p4, 1, VM_CTX_ETH };
    CHECK(vm_interpreter_run(&prog4, dummy, 4, NULL) == VM_VERDICT_DROP, "RET imm=42 -> DROP");
}

/* ------------------------------------------------------------------ */
/* ISA v1 conformance: one test per rule in docs/ISA.md                */
/* ------------------------------------------------------------------ */

/* docs/ISA.md section 4. Hex values are written out on purpose: the
 * assembler and stored .bin programs depend on them, so a changed macro
 * value must fail here. */
static const struct { const char *name; unsigned macro; unsigned hex; } isa_v1_opcodes[] = {
    { "LDPKT_B", VM_OP_LDPKT_B, 0x00 }, { "LDPKT_H", VM_OP_LDPKT_H, 0x08 }, { "LDPKT_W", VM_OP_LDPKT_W, 0x10 },
    { "ADD_K", VM_OP_ADD_K, 0x01 }, { "SUB_K", VM_OP_SUB_K, 0x09 }, { "AND_K", VM_OP_AND_K, 0x11 },
    { "OR_K",  VM_OP_OR_K,  0x19 }, { "XOR_K", VM_OP_XOR_K, 0x21 }, { "LSH_K", VM_OP_LSH_K, 0x29 },
    { "RSH_K", VM_OP_RSH_K, 0x31 },
    { "ADD_X", VM_OP_ADD_X, 0x02 }, { "SUB_X", VM_OP_SUB_X, 0x0A }, { "AND_X", VM_OP_AND_X, 0x12 },
    { "OR_X",  VM_OP_OR_X,  0x1A }, { "XOR_X", VM_OP_XOR_X, 0x22 }, { "LSH_X", VM_OP_LSH_X, 0x2A },
    { "RSH_X", VM_OP_RSH_X, 0x32 },
    { "JMP",   VM_OP_JMP,   0x03 },
    { "JEQ_K", VM_OP_JEQ_K, 0x0B }, { "JNE_K", VM_OP_JNE_K, 0x13 }, { "JGT_K", VM_OP_JGT_K, 0x1B },
    { "JGE_K", VM_OP_JGE_K, 0x23 }, { "JLT_K", VM_OP_JLT_K, 0x2B }, { "JLE_K", VM_OP_JLE_K, 0x33 },
    { "JEQ_X", VM_OP_JEQ_X, 0x0C }, { "JNE_X", VM_OP_JNE_X, 0x14 }, { "JGT_X", VM_OP_JGT_X, 0x1C },
    { "JGE_X", VM_OP_JGE_X, 0x24 }, { "JLT_X", VM_OP_JLT_X, 0x2C }, { "JLE_X", VM_OP_JLE_X, 0x34 },
    { "MOV_K", VM_OP_MOV_K, 0x06 }, { "MOV_X", VM_OP_MOV_X, 0x0E },
    { "RET",   VM_OP_RET,   0x05 },
};
#define N_ISA_V1_OPCODES (sizeof isa_v1_opcodes / sizeof isa_v1_opcodes[0])

static int is_isa_v1_opcode(unsigned op)
{
    for (size_t i = 0; i < N_ISA_V1_OPCODES; i++) {
        if (isa_v1_opcodes[i].hex == op) return 1;
    }
    return 0;
}

/* docs/ISA.md section 4: the fields each opcode uses. All others must be 0 (Q1). */
enum { USES_DST = 1u, USES_SRC = 2u, USES_OFF = 4u, USES_IMM = 8u };

static unsigned isa_v1_uses(unsigned op)
{
    switch (op) {
    case 0x00: case 0x08: case 0x10:
        return USES_DST | USES_SRC | USES_OFF;                                   /* LDPKT_*  */
    case 0x01: case 0x09: case 0x11: case 0x19: case 0x21: case 0x29: case 0x31:
        return USES_DST | USES_IMM;                                              /* ALU _K   */
    case 0x02: case 0x0A: case 0x12: case 0x1A: case 0x22: case 0x2A: case 0x32:
        return USES_DST | USES_SRC;                                              /* ALU _X   */
    case 0x03:
        return USES_OFF;                                                         /* JMP      */
    case 0x0B: case 0x13: case 0x1B: case 0x23: case 0x2B: case 0x33:
        return USES_DST | USES_OFF | USES_IMM;                                   /* Jcc _K   */
    case 0x0C: case 0x14: case 0x1C: case 0x24: case 0x2C: case 0x34:
        return USES_DST | USES_SRC | USES_OFF;                                   /* Jcc _X   */
    case 0x06:
        return USES_DST | USES_IMM;                                              /* MOV_K    */
    case 0x0E:
        return USES_DST | USES_SRC;                                              /* MOV_X    */
    default:
        return USES_IMM;                                                         /* RET 0x05 */
    }
}

#define MAX_TEST_INSNS 16u

/* Verify a test program (it must pass) and run it. */
static int run_verified(const vm_insn_t *p, unsigned n, const uint8_t *pkt, uint32_t len,
                        vm_run_stats_t *st, const char *what)
{
    vm_program_t prog = { p, (uint16_t)n, VM_CTX_ETH };
    if (vm_verify(&prog) != VM_VERIFY_OK) {
        g_fail++;
        printf("  FAIL: %s: test program did not verify\n", what);
        return -1;
    }
    return vm_interpreter_run(&prog, pkt, len, st);
}

/* Runs body, then "PASS iff r1 == expected" (r7 is scratch, so body must
 * not rely on r7). Returns 1 if r1 had the expected value. */
static int r1_is(const vm_insn_t *body, unsigned nbody, const uint8_t *pkt, uint32_t len,
                 uint32_t expected, vm_run_stats_t *st, const char *what)
{
    vm_insn_t p[MAX_TEST_INSNS];
    unsigned n = 0;
    for (unsigned i = 0; i < nbody && n < MAX_TEST_INSNS - 4u; i++) p[n++] = body[i];
    p[n++] = (vm_insn_t){ VM_OP_MOV_K, 7, 0, 0, (int32_t)expected };
    p[n++] = (vm_insn_t){ VM_OP_JEQ_X, 1, 7, 1, 0 };
    p[n++] = (vm_insn_t){ VM_OP_RET,   0, 0, 0, VM_VERDICT_DROP };
    p[n++] = (vm_insn_t){ VM_OP_RET,   0, 0, 0, VM_VERDICT_PASS };
    return run_verified(p, n, pkt, len, st, what) == VM_VERDICT_PASS;
}

static void test_isa_v1_encoding(void)
{
    printf("[ISA v1] opcode values and byte layout (docs/ISA.md sections 3-4)\n");

    unsigned bad = 0;
    for (size_t i = 0; i < N_ISA_V1_OPCODES; i++) {
        if (isa_v1_opcodes[i].macro != isa_v1_opcodes[i].hex) {
            bad++;
            printf("  %s is 0x%02X, ISA.md says 0x%02X\n", isa_v1_opcodes[i].name,
                   isa_v1_opcodes[i].macro, isa_v1_opcodes[i].hex);
        }
    }
    CHECK(N_ISA_V1_OPCODES == 33u, "ISA v1 has 33 opcodes");
    CHECK(bad == 0, "every opcode macro matches its value in ISA.md");

    /* Byte layout: opcode | dst (bits 0-3) + src (bits 4-7) | offset LE | imm LE */
    const vm_insn_t sample = { VM_OP_JNE_K, 3, 5, -2, 0x12345678 };
    static const uint8_t expect[8] = { 0x13, 0x53, 0xFE, 0xFF, 0x78, 0x56, 0x34, 0x12 };
    uint8_t got[8];
    memcpy(got, &sample, sizeof got);
    CHECK(memcmp(got, expect, sizeof got) == 0, "instruction bytes match the ISA.md layout");

    /* Exhaustive: the verifier accepts exactly the 33 ISA v1 opcodes. */
    unsigned accepted = 0, misclassified = 0;
    for (unsigned op = 0; op < 256u; op++) {
        const vm_insn_t p[2] = { { (uint8_t)op, 0, 0, 0, 0 }, { VM_OP_RET, 0, 0, 0, VM_VERDICT_PASS } };
        vm_program_t prog = { p, 2, VM_CTX_ETH };
        vm_verify_result_t r = vm_verify(&prog);
        if (r == VM_VERIFY_OK) accepted++;
        int ok = is_isa_v1_opcode(op) ? (r == VM_VERIFY_OK) : (r == VM_VERIFY_ERR_BAD_OPCODE);
        if (!ok) {
            misclassified++;
            printf("  opcode 0x%02X: verifier says %s\n", op, vm_verify_strerror(r));
        }
    }
    CHECK(misclassified == 0, "all 256 opcode values accepted/rejected as ISA.md says");
    CHECK(accepted == 33u, "verifier accepts exactly 33 opcodes");

    /* Q4: class 7 is reserved; all 32 of its opcodes are rejected. */
    unsigned reserved_rejected = 0;
    for (unsigned hi = 0; hi < 32u; hi++) {
        const vm_insn_t p[2] = { { (uint8_t)((hi << 3) | VM_CLASS_RESERVED), 0, 0, 0, 0 },
                                 { VM_OP_RET, 0, 0, 0, VM_VERDICT_PASS } };
        vm_program_t prog = { p, 2, VM_CTX_ETH };
        if (vm_verify(&prog) == VM_VERIFY_ERR_BAD_OPCODE) reserved_rejected++;
    }
    CHECK(VM_CLASS_RESERVED == 7u && reserved_rejected == 32u, "reserved class 7: all 32 opcodes rejected (Q4)");
    CHECK(VM_ISA_VERSION == 1u, "ISA version is 1");
}

static void test_isa_v1_verifier_rules(void)
{
    printf("[ISA v1] verifier rules V1-V10, boundaries and error order\n");

    vm_program_t null_insns = { NULL, 3, VM_CTX_ETH };
    CHECK(vm_verify(&null_insns) == VM_VERIFY_ERR_EMPTY, "V1: NULL instruction pointer rejected");

    /* V3 (Q5): the program declares a known context; 0 (forgotten field) is invalid. */
    const vm_insn_t ok1[] = { { VM_OP_RET, 0, 0, 0, VM_VERDICT_PASS } };
    vm_program_t c_eth = { ok1, 1, VM_CTX_ETH }, c_ip = { ok1, 1, VM_CTX_IPV4 };
    vm_program_t c_0 = { ok1, 1, 0 }, c_3 = { ok1, 1, 3 }, c_255 = { ok1, 1, 255 };
    CHECK(vm_verify(&c_eth) == VM_VERIFY_OK, "V3: VM_CTX_ETH accepted");
    CHECK(vm_verify(&c_ip)  == VM_VERIFY_OK, "V3: VM_CTX_IPV4 accepted");
    CHECK(vm_verify(&c_0) == VM_VERIFY_ERR_BAD_CTX && vm_verify(&c_3) == VM_VERIFY_ERR_BAD_CTX &&
          vm_verify(&c_255) == VM_VERIFY_ERR_BAD_CTX, "V3: context 0, 3, 255 rejected");
    CHECK(ddos_program_eth.ctx == VM_CTX_ETH && ddos_program_ip.ctx == VM_CTX_IPV4,
          "DDoS programs declare the hook they were written for");
    const vm_insn_t badop_noret[] = { { 0xFF, 0, 0, 0, 0 } };
    vm_program_t c_order = { badop_noret, 1, 0 };
    vm_program_t c_empty = { NULL, 0, 0 };
    CHECK(vm_verify(&c_order) == VM_VERIFY_ERR_BAD_CTX, "order: context checked before instructions");
    CHECK(vm_verify(&c_empty) == VM_VERIFY_ERR_EMPTY,   "order: empty program reported before context");

    /* V2: exactly VM_MAX_INSNS is allowed, one more is not. */
    static vm_insn_t big[VM_MAX_INSNS + 1u];
    for (unsigned i = 0; i < VM_MAX_INSNS + 1u; i++) big[i] = (vm_insn_t){ VM_OP_MOV_K, 1, 0, 0, 0 };
    big[VM_MAX_INSNS - 1u] = (vm_insn_t){ VM_OP_RET, 0, 0, 0, VM_VERDICT_PASS };
    big[VM_MAX_INSNS]      = (vm_insn_t){ VM_OP_RET, 0, 0, 0, VM_VERDICT_PASS };
    vm_program_t at_max = { big, (uint16_t)VM_MAX_INSNS, VM_CTX_ETH };
    vm_program_t over   = { big, (uint16_t)(VM_MAX_INSNS + 1u), VM_CTX_ETH };
    CHECK(vm_verify(&at_max) == VM_VERIFY_OK, "V2: 512-instruction program accepted");
    CHECK(vm_verify(&over) == VM_VERIFY_ERR_TOO_LONG, "V2: 513-instruction program rejected");

    /* V4: register bounds, also in fields the instruction does not use. */
    const vm_insn_t r7[]  = { { VM_OP_MOV_K, 7, 0, 0, 1 }, { VM_OP_RET, 0, 0, 0, 1 } };
    const vm_insn_t d8[]  = { { VM_OP_MOV_K, 8, 0, 0, 1 }, { VM_OP_RET, 0, 0, 0, 1 } };
    const vm_insn_t s8[]  = { { VM_OP_MOV_X, 1, 8, 0, 0 }, { VM_OP_RET, 0, 0, 0, 1 } };
    const vm_insn_t s15[] = { { VM_OP_MOV_K, 1, 15, 0, 0 }, { VM_OP_RET, 0, 0, 0, 1 } };
    const vm_insn_t rt8[] = { { VM_OP_RET, 8, 0, 0, 1 } };
    vm_program_t pr7 = { r7, 2, VM_CTX_ETH }, pd8 = { d8, 2, VM_CTX_ETH }, ps8 = { s8, 2, VM_CTX_ETH }, ps15 = { s15, 2, VM_CTX_ETH }, prt8 = { rt8, 1, VM_CTX_ETH };
    CHECK(vm_verify(&pr7)  == VM_VERIFY_OK,          "V5: r7 accepted");
    CHECK(vm_verify(&pd8)  == VM_VERIFY_ERR_BAD_REG, "V5: dst r8 rejected");
    CHECK(vm_verify(&ps8)  == VM_VERIFY_ERR_BAD_REG, "V5: src r8 rejected");
    CHECK(vm_verify(&ps15) == VM_VERIFY_ERR_BAD_REG, "V5: src 15 rejected even where src is unused (MOV_K)");
    CHECK(vm_verify(&prt8) == VM_VERIFY_ERR_BAD_REG, "V5: dst 8 rejected even on RET");

    /* V5/V6: forward-only, in-range branch targets. */
    const vm_insn_t off0[]  = { { VM_OP_JMP, 0, 0, 0, 0 }, { VM_OP_RET, 0, 0, 0, 1 } };
    const vm_insn_t tolast[] = { { VM_OP_JEQ_K, 1, 0, 1, 0 }, { VM_OP_MOV_K, 1, 0, 0, 0 },
                                 { VM_OP_RET, 0, 0, 0, 1 } };
    const vm_insn_t past[]  = { { VM_OP_JEQ_K, 1, 0, 2, 0 }, { VM_OP_MOV_K, 1, 0, 0, 0 },
                                { VM_OP_RET, 0, 0, 0, 1 } };
    const vm_insn_t backx[] = { { VM_OP_MOV_K, 1, 0, 0, 0 }, { VM_OP_JEQ_X, 1, 2, -2, 0 },
                                { VM_OP_RET, 0, 0, 0, 1 } };
    vm_program_t po0 = { off0, 2, VM_CTX_ETH }, ptl = { tolast, 3, VM_CTX_ETH }, ppa = { past, 3, VM_CTX_ETH }, pbx = { backx, 3, VM_CTX_ETH };
    CHECK(vm_verify(&po0) == VM_VERIFY_OK,                  "V8: offset 0 (next instruction) accepted");
    CHECK(vm_verify(&ptl) == VM_VERIFY_OK,                  "V9: jump to the last instruction accepted");
    CHECK(vm_verify(&ppa) == VM_VERIFY_ERR_BAD_JUMP,        "V9: jump to n (one past the end) rejected");
    CHECK(vm_verify(&pbx) == VM_VERIFY_ERR_BACKWARD_JUMP,   "V8: backward register-compare branch rejected");

    /* Error order: whole-program checks, then pc order, opcode before registers. */
    const vm_insn_t both[]  = { { 0xFF, 9, 9, 0, 0 }, { VM_OP_RET, 0, 0, 0, 1 } };
    const vm_insn_t first[] = { { VM_OP_MOV_K, 9, 0, 0, 0 }, { VM_OP_JMP, 0, 0, -2, 0 },
                                { VM_OP_RET, 0, 0, 0, 1 } };
    const vm_insn_t scan[]  = { { 0xFF, 0, 0, 0, 0 }, { VM_OP_MOV_K, 1, 0, 0, 0 } };
    const vm_insn_t unbr[] = { { VM_OP_JMP, 0, 0, -1, 1 }, { VM_OP_RET, 0, 0, 0, 1 } };
    vm_program_t pboth = { both, 2, VM_CTX_ETH }, pfirst = { first, 3, VM_CTX_ETH }, pscan = { scan, 2, VM_CTX_ETH }, punbr = { unbr, 2, VM_CTX_ETH };
    CHECK(vm_verify(&pboth)  == VM_VERIFY_ERR_BAD_OPCODE,   "order: bad opcode reported before bad register");
    CHECK(vm_verify(&pfirst) == VM_VERIFY_ERR_BAD_REG,      "order: first faulty instruction (lowest pc) wins");
    CHECK(vm_verify(&pscan)  == VM_VERIFY_ERR_BAD_OPCODE,   "order: instruction errors before missing RET");
    CHECK(vm_verify(&punbr)  == VM_VERIFY_ERR_UNUSED_FIELD, "order: unused-field check before branch target");

    /* Every result code has its own message. */
    const char *unknown = vm_verify_strerror((vm_verify_result_t)99);
    unsigned bad = 0;
    for (int r = VM_VERIFY_OK; r <= VM_VERIFY_ERR_BAD_CTX; r++) {
        const char *s = vm_verify_strerror((vm_verify_result_t)r);
        if (s == NULL || s[0] == '\0' || strcmp(s, unknown) == 0) bad++;
    }
    CHECK(bad == 0, "vm_verify_strerror has a message for every result code");
    CHECK(strcmp(unknown, "unknown verification error") == 0, "vm_verify_strerror handles unknown codes");
}

/* Q1: for each field, setting it to 1 must be rejected exactly where the
 * opcode does not use it. Program: insn, RET, RET (so offset 1 is a valid branch). */
static void test_isa_v1_unused_fields(void)
{
    static const char *const field_names[] = { "dst", "src", "offset", "imm" };
    char msg[96];
    printf("[ISA v1] canonical encoding: unused fields must be 0 (Q1)\n");

    for (unsigned f = 0; f < 4u; f++) {
        unsigned bad = 0;
        for (size_t i = 0; i < N_ISA_V1_OPCODES; i++) {
            unsigned op = isa_v1_opcodes[i].hex;
            vm_insn_t p[3] = { { (uint8_t)op, 0, 0, 0, 0 },
                               { VM_OP_RET, 0, 0, 0, VM_VERDICT_PASS },
                               { VM_OP_RET, 0, 0, 0, VM_VERDICT_PASS } };
            switch (f) {
            case 0:  p[0].dst_reg = 1; break;
            case 1:  p[0].src_reg = 1; break;
            case 2:  p[0].offset  = 1; break;
            default: p[0].imm     = 1; break;
            }
            vm_program_t prog = { p, 3, VM_CTX_ETH };
            vm_verify_result_t want = (isa_v1_uses(op) & (1u << f)) ? VM_VERIFY_OK
                                                                     : VM_VERIFY_ERR_UNUSED_FIELD;
            vm_verify_result_t got = vm_verify(&prog);
            if (got != want) {
                bad++;
                printf("  %s with %s=1: verifier says %s\n", isa_v1_opcodes[i].name, field_names[f],
                       vm_verify_strerror(got));
            }
        }
        snprintf(msg, sizeof msg, "%s != 0: rejected where unused, accepted where used (33 opcodes)",
                 field_names[f]);
        CHECK(bad == 0, msg);
    }

    const vm_insn_t neg[] = { { VM_OP_LDPKT_B, 1, 0, 0, -1 }, { VM_OP_RET, 0, 0, 0, 1 } };
    vm_program_t pneg = { neg, 2, VM_CTX_ETH };
    CHECK(vm_verify(&pneg) == VM_VERIFY_ERR_UNUSED_FIELD, "negative value in an unused field rejected");
}

static uint32_t ref_alu(unsigned alu_op, uint32_t a, uint32_t b)
{
    switch (alu_op) {
    case VM_ALU_ADD: return a + b;
    case VM_ALU_SUB: return a - b;
    case VM_ALU_AND: return a & b;
    case VM_ALU_OR:  return a | b;
    case VM_ALU_XOR: return a ^ b;
    case VM_ALU_LSH: return a << (b & 31u);
    default:         return a >> (b & 31u);    /* VM_ALU_RSH: logical shift */
    }
}

static int ref_jmp(unsigned jmp_op, uint32_t a, uint32_t b)
{
    switch (jmp_op) {
    case VM_JMP_JEQ: return a == b;
    case VM_JMP_JNE: return a != b;
    case VM_JMP_JGT: return a >  b;
    case VM_JMP_JGE: return a >= b;
    case VM_JMP_JLT: return a <  b;
    default:         return a <= b;            /* VM_JMP_JLE */
    }
}

/* Operand pairs chosen to hit wraparound, sign bits, shift masking and equality. */
static const uint32_t operand_pairs[][2] = {
    { 0, 0 }, { 5, 5 }, { 5, 6 }, { 6, 5 }, { 0xFFFFFFFFu, 1 }, { 0, 1 }, { 0x80000000u, 31 },
    { 0x80000000u, 0x7FFFFFFFu }, { 0x12345678u, 32 }, { 0x12345678u, 33 }, { 5, 0xFFFFFFFFu },
    { 0xFFFFFFFFu, 0 }, { 0xF0F0F0F0u, 0x0FF00FF0u },
};
#define N_OPERAND_PAIRS (sizeof operand_pairs / sizeof operand_pairs[0])

static void test_isa_v1_alu(void)
{
    static const char *const names[] = { "ADD", "SUB", "AND", "OR", "XOR", "LSH", "RSH" };
    static const uint8_t pkt[1] = { 0 };
    char msg[96];

    printf("[ISA v1] ALU semantics vs a C reference model\n");
    for (unsigned op = VM_ALU_ADD; op <= VM_ALU_RSH; op++) {
        for (unsigned form = 0; form < 2u; form++) {          /* 0: _K (imm), 1: _X (register) */
            uint8_t opcode = (uint8_t)((op << 3) | (form ? VM_CLASS_ALU_X : VM_CLASS_ALU_K));
            unsigned bad = 0;
            for (size_t i = 0; i < N_OPERAND_PAIRS; i++) {
                uint32_t a = operand_pairs[i][0], b = operand_pairs[i][1];
                const vm_insn_t body[] = {
                    { VM_OP_MOV_K, 1, 0, 0, (int32_t)a },
                    { VM_OP_MOV_K, 2, 0, 0, (int32_t)b },
                    { opcode, 1, (uint8_t)(form ? 2 : 0), 0, form ? 0 : (int32_t)b },
                };
                if (!r1_is(body, 3, pkt, sizeof pkt, ref_alu(op, a, b), NULL, names[op])) {
                    bad++;
                    printf("  %s_%c a=0x%08X b=0x%08X: wrong result\n", names[op], form ? 'X' : 'K',
                           (unsigned)a, (unsigned)b);
                }
            }
            snprintf(msg, sizeof msg, "%s_%c matches the reference on %u operand pairs",
                     names[op], form ? 'X' : 'K', (unsigned)N_OPERAND_PAIRS);
            CHECK(bad == 0, msg);
        }
    }
}

static void test_isa_v1_branches(void)
{
    static const char *const names[] = { "JMP", "JEQ", "JNE", "JGT", "JGE", "JLT", "JLE" };
    static const uint8_t pkt[1] = { 0 };
    char msg[96];

    printf("[ISA v1] branch semantics (unsigned compares) vs a C reference model\n");
    for (unsigned op = VM_JMP_JEQ; op <= VM_JMP_JLE; op++) {
        for (unsigned form = 0; form < 2u; form++) {          /* 0: _K (imm), 1: _X (register) */
            uint8_t opcode = (uint8_t)((op << 3) | (form ? VM_CLASS_JMP_X : VM_CLASS_JMP_K));
            unsigned bad = 0;
            for (size_t i = 0; i < N_OPERAND_PAIRS; i++) {
                uint32_t a = operand_pairs[i][0], b = operand_pairs[i][1];
                const vm_insn_t p[] = {
                    { VM_OP_MOV_K, 1, 0, 0, (int32_t)a },
                    { VM_OP_MOV_K, 2, 0, 0, (int32_t)b },
                    { opcode, 1, (uint8_t)(form ? 2 : 0), 1, form ? 0 : (int32_t)b },
                    { VM_OP_RET, 0, 0, 0, VM_VERDICT_DROP },  /* not taken */
                    { VM_OP_RET, 0, 0, 0, VM_VERDICT_PASS },  /* taken     */
                };
                int taken = run_verified(p, 5, pkt, sizeof pkt, NULL, names[op]) == VM_VERDICT_PASS;
                if (taken != ref_jmp(op, a, b)) {
                    bad++;
                    printf("  %s_%c a=0x%08X b=0x%08X: taken=%d\n", names[op], form ? 'X' : 'K',
                           (unsigned)a, (unsigned)b, taken);
                }
            }
            snprintf(msg, sizeof msg, "%s_%c matches the reference on %u operand pairs",
                     names[op], form ? 'X' : 'K', (unsigned)N_OPERAND_PAIRS);
            CHECK(bad == 0, msg);
        }
    }

    const vm_insn_t skip[] = { { VM_OP_JMP, 0, 0, 1, 0 }, { VM_OP_RET, 0, 0, 0, VM_VERDICT_DROP },
                               { VM_OP_RET, 0, 0, 0, VM_VERDICT_PASS } };
    const vm_insn_t nop[]  = { { VM_OP_JMP, 0, 0, 0, 0 }, { VM_OP_RET, 0, 0, 0, VM_VERDICT_PASS } };
    CHECK(run_verified(skip, 3, pkt, sizeof pkt, NULL, "JMP +1") == VM_VERDICT_PASS, "JMP +1 skips one instruction");
    CHECK(run_verified(nop, 2, pkt, sizeof pkt, NULL, "JMP +0") == VM_VERDICT_PASS, "JMP +0 is a no-op");
}

static void test_isa_v1_loads(void)
{
    static const uint8_t pkt[5] = { 0x12, 0x34, 0x56, 0x78, 0x9A };
    static const struct {
        uint8_t op; uint32_t base; int16_t off; uint32_t len; int in_bounds; uint32_t value; const char *what;
    } cases[] = {
        { VM_OP_LDPKT_B, 0, 0, 5, 1, 0x12,        "LDPKT_B at offset 0" },
        { VM_OP_LDPKT_B, 0, 4, 5, 1, 0x9A,        "LDPKT_B at the last byte" },
        { VM_OP_LDPKT_B, 0, 5, 5, 0, 0,           "LDPKT_B one past the end -> DROP" },
        { VM_OP_LDPKT_H, 0, 0, 5, 1, 0x1234,      "LDPKT_H is big-endian" },
        { VM_OP_LDPKT_H, 0, 3, 5, 1, 0x789A,      "LDPKT_H ending at the last byte" },
        { VM_OP_LDPKT_H, 0, 4, 5, 0, 0,           "LDPKT_H crossing the end -> DROP" },
        { VM_OP_LDPKT_W, 0, 0, 5, 1, 0x12345678u, "LDPKT_W is big-endian" },
        { VM_OP_LDPKT_W, 0, 1, 5, 1, 0x3456789Au, "LDPKT_W ending at the last byte" },
        { VM_OP_LDPKT_W, 0, 2, 5, 0, 0,           "LDPKT_W crossing the end -> DROP" },
        { VM_OP_LDPKT_B, 3, 1, 5, 1, 0x9A,        "register base + offset" },
        { VM_OP_LDPKT_B, 3, -3, 5, 1, 0x12,       "negative offset from a register base" },
        { VM_OP_LDPKT_B, 0xFFFFFFFFu, 1, 5, 1, 0x12, "base + offset wraps mod 2^32 (to 0)" },
        { VM_OP_LDPKT_B, 0xFFFFFFFFu, 0, 5, 0, 0, "address 0xFFFFFFFF -> DROP" },
        { VM_OP_LDPKT_B, 0, -32768, 5, 0, 0,      "most negative offset -> DROP" },
        { VM_OP_LDPKT_B, 0, 0, 0, 0, 0,           "empty packet (len 0) -> DROP" },
        { VM_OP_LDPKT_H, 0, 0, 1, 0, 0,           "load wider than the packet -> DROP" },
    };

    printf("[ISA v1] packet loads: byte order, addressing, bounds\n");
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        vm_run_stats_t st;
        const vm_insn_t body[] = {
            { VM_OP_MOV_K, 2, 0, 0, (int32_t)cases[i].base },
            { cases[i].op, 1, 2, cases[i].off, 0 },
        };
        if (cases[i].in_bounds) {
            int ok = r1_is(body, 2, pkt, cases[i].len, cases[i].value, &st, cases[i].what);
            CHECK(ok && st.aborted_oob == 0, cases[i].what);
        } else {
            /* Without the fault this program would PASS, so DROP here can only come from the bounds check. */
            const vm_insn_t p[] = { body[0], body[1], { VM_OP_RET, 0, 0, 0, VM_VERDICT_PASS } };
            int v = run_verified(p, 3, pkt, cases[i].len, &st, cases[i].what);
            CHECK(v == VM_VERDICT_DROP && st.aborted_oob == 1, cases[i].what);
        }
    }
}

static void test_isa_v1_moves_and_ret(void)
{
    static const uint8_t pkt[1] = { 0 };
    printf("[ISA v1] registers, moves, RET values\n");

    /* All registers start at 0 (r1 is compared directly, r2..r6 through MOV_X). */
    unsigned nonzero = 0;
    for (uint8_t r = 0; r < 7u; r++) {
        const vm_insn_t body[] = { { VM_OP_MOV_X, 1, r, 0, 0 } };
        if (!r1_is(body, 1, pkt, sizeof pkt, 0, NULL, "zeroed registers")) nonzero++;
    }
    CHECK(nonzero == 0, "registers r0-r6 are 0 at program start");

    /* r7 too (r1_is uses r7 as scratch). Fill every register with junk in one
     * run first: a missed initialisation would then see the stale stack value. */
    static const vm_insn_t junk[] = {
        { VM_OP_MOV_K, 0, 0, 0, -1 }, { VM_OP_MOV_K, 1, 0, 0, -1 }, { VM_OP_MOV_K, 2, 0, 0, -1 },
        { VM_OP_MOV_K, 3, 0, 0, -1 }, { VM_OP_MOV_K, 4, 0, 0, -1 }, { VM_OP_MOV_K, 5, 0, 0, -1 },
        { VM_OP_MOV_K, 6, 0, 0, -1 }, { VM_OP_MOV_K, 7, 0, 0, -1 }, { VM_OP_RET, 0, 0, 0, 1 } };
    static const vm_insn_t r7zero[] = { { VM_OP_JEQ_K, 7, 0, 1, 0 }, { VM_OP_RET, 0, 0, 0, 0 },
                                        { VM_OP_RET, 0, 0, 0, 1 } };
    (void)run_verified(junk, 9, pkt, sizeof pkt, NULL, "junk registers");
    CHECK(run_verified(r7zero, 3, pkt, sizeof pkt, NULL, "r7 zero") == VM_VERDICT_PASS,
          "r7 is 0 at program start, even after a run that left junk in it");

    const vm_insn_t movk[] = { { VM_OP_MOV_K, 1, 0, 0, -2 } };
    CHECK(r1_is(movk, 1, pkt, sizeof pkt, 0xFFFFFFFEu, NULL, "MOV_K -2"), "MOV_K sign-extends imm");
    const vm_insn_t movx[] = { { VM_OP_MOV_K, 3, 0, 0, 0x5A5A }, { VM_OP_MOV_X, 1, 3, 0, 0 } };
    CHECK(r1_is(movx, 2, pkt, sizeof pkt, 0x5A5A, NULL, "MOV_X"), "MOV_X copies a register");

    const vm_insn_t ret1[] = { { VM_OP_RET, 0, 0, 0, 1 } };
    const vm_insn_t ret0[] = { { VM_OP_RET, 0, 0, 0, 0 } };
    CHECK(run_verified(ret1, 1, pkt, sizeof pkt, NULL, "RET 1") == VM_VERDICT_PASS, "RET 1 -> PASS");
    CHECK(run_verified(ret0, 1, pkt, sizeof pkt, NULL, "RET 0") == VM_VERDICT_DROP, "RET 0 -> DROP");

    /* Q2: the verifier rejects any other RET value; the interpreter still
     * treats it as DROP if an unverified program reaches it (defence in depth). */
    static const int32_t bad_rets[] = { 2, -1, 42, INT32_MIN };
    unsigned rejected = 0, dropped = 0;
    for (size_t i = 0; i < sizeof bad_rets / sizeof bad_rets[0]; i++) {
        const vm_insn_t p[] = { { VM_OP_RET, 0, 0, 0, bad_rets[i] } };
        vm_program_t prog = { p, 1, VM_CTX_ETH };
        if (vm_verify(&prog) == VM_VERIFY_ERR_BAD_RET) rejected++;
        if (vm_interpreter_run(&prog, pkt, sizeof pkt, NULL) == VM_VERDICT_DROP) dropped++;
    }
    CHECK(rejected == 4u, "verifier rejects RET 2, -1, 42, INT32_MIN (Q2)");
    CHECK(dropped == 4u,  "interpreter still returns DROP for those values if unverified");

    /* A bad RET that is not the last instruction is caught too. */
    const vm_insn_t mid[] = { { VM_OP_JEQ_K, 1, 0, 1, 0 }, { VM_OP_RET, 0, 0, 0, 7 },
                              { VM_OP_RET, 0, 0, 0, 1 } };
    vm_program_t pmid = { mid, 3, VM_CTX_ETH };
    CHECK(vm_verify(&pmid) == VM_VERIFY_ERR_BAD_RET, "bad RET in the middle of a program rejected");
}

/* Defence in depth (ISA.md section 8). These programs deliberately FAIL
 * verification. Running them is only safe because they use registers < 8. */
static void test_runtime_guards_unverified(void)
{
    static const uint8_t pkt[4] = { 0 };
    vm_run_stats_t st;
    printf("[ISA v1] interpreter guards on unverified programs\n");

    static const vm_insn_t loop[] = { { VM_OP_JMP, 0, 0, -1, 0 } };
    vm_program_t pl = { loop, 1, VM_CTX_ETH };
    CHECK(vm_verify(&pl) == VM_VERIFY_ERR_BACKWARD_JUMP, "infinite loop is rejected by the verifier");
    CHECK(vm_interpreter_run(&pl, pkt, sizeof pkt, &st) == VM_VERDICT_DROP && st.aborted_budget == 1,
          "unverified infinite loop is stopped by the step budget -> DROP");

    static const vm_insn_t fall[] = { { VM_OP_MOV_K, 1, 0, 0, 1 } };
    vm_program_t pf = { fall, 1, VM_CTX_ETH };
    CHECK(vm_interpreter_run(&pf, pkt, sizeof pkt, &st) == VM_VERDICT_DROP && st.aborted_oob == 1,
          "running off the end of an unverified program -> DROP");

    static const vm_insn_t unk[] = { { 0xFF, 0, 0, 0, 0 }, { VM_OP_RET, 0, 0, 0, VM_VERDICT_PASS } };
    vm_program_t pu = { unk, 2, VM_CTX_ETH };
    CHECK(vm_interpreter_run(&pu, pkt, sizeof pkt, &st) == VM_VERDICT_DROP && st.aborted_oob == 1,
          "unknown opcode at run time -> DROP");
}

/* D11: insns_executed is filled in on every exit path, not only on RET. */
static void test_insns_executed_all_paths(void)
{
    uint8_t f[256];
    uint32_t n;
    vm_run_stats_t st;
    printf("[D11] insns_executed on every exit path\n");

    frame_spec_t udp_attack = { 0x0800, 5, 17, 0x4000, 40000, DDOS_TARGET_PORT, 32 };
    n = build_frame(f, sizeof f, &udp_attack);
    CHECK(run_eth(f, n, &st) == VM_VERDICT_DROP && st.insns_executed == 7,
          "DDoS DROP path executes 7 instructions");

    frame_spec_t arp = { 0x0806, 0, 0, 0, 0, 0, 0 };
    n = build_frame(f, sizeof f, &arp);
    CHECK(run_eth(f, n, &st) == VM_VERDICT_PASS && st.insns_executed == 3,
          "DDoS non-IPv4 PASS path executes 3 instructions");

    /* Truncated IPv4 frame: insn 0 (EtherType) and 1 run, insn 2 (byte 23)
     * faults -> 3 instructions started, the third one faulted. */
    n = build_frame(f, sizeof f, &udp_attack);
    CHECK(run_eth(f, 20, &st) == VM_VERDICT_DROP && st.aborted_oob == 1 && st.insns_executed == 3,
          "OOB fault on 3rd instruction reports insns_executed == 3 (was 0)");

    /* Frame too short even for the EtherType: the first load faults. */
    CHECK(run_eth(f, 10, &st) == VM_VERDICT_DROP && st.aborted_oob == 1 && st.insns_executed == 1,
          "OOB fault on 1st instruction reports insns_executed == 1");

    /* NULL packet: nothing runs. */
    st.insns_executed = 99;
    CHECK(vm_interpreter_run(&ddos_program_eth, NULL, 64, &st) == VM_VERDICT_DROP &&
          st.insns_executed == 0, "NULL packet reports insns_executed == 0");

    /* Unverified infinite loop: stopped after exactly VM_MAX_STEPS. */
    static const uint8_t pkt[4] = { 0 };
    static const vm_insn_t loop[] = { { VM_OP_JMP, 0, 0, -1, 0 } };
    vm_program_t pl = { loop, 1, VM_CTX_ETH };
    CHECK(vm_interpreter_run(&pl, pkt, sizeof pkt, &st) == VM_VERDICT_DROP &&
          st.aborted_budget == 1 && st.insns_executed == VM_MAX_STEPS,
          "budget stop reports insns_executed == VM_MAX_STEPS");

    /* Running off the end of an unverified program: 1 instruction ran. */
    static const vm_insn_t fall[] = { { VM_OP_MOV_K, 1, 0, 0, 1 } };
    vm_program_t pf = { fall, 1, VM_CTX_ETH };
    CHECK(vm_interpreter_run(&pf, pkt, sizeof pkt, &st) == VM_VERDICT_DROP &&
          st.insns_executed == 1, "falling off the end reports insns_executed == 1");
}

/* D9: programs reach a hook only through vm_slot_install(), which verifies
 * them and checks that they were written for that hook (ctx). */
static void test_vm_slot(void)
{
    vm_slot_t eth, ip;
    vm_verify_result_t why;
    printf("[D9] vm_slot: verify-before-install and ctx check\n");

    vm_slot_init(&eth, VM_CTX_ETH);
    vm_slot_init(&ip, VM_CTX_IPV4);
    CHECK(vm_slot_get(&eth) == NULL, "new slot is empty");

    CHECK(vm_slot_install(&eth, &ddos_program_eth, &why) == VM_SLOT_OK && why == VM_VERIFY_OK &&
          vm_slot_get(&eth) == &ddos_program_eth, "ETH program installs into the driver-hook slot");
    CHECK(vm_slot_install(&ip, &ddos_program_ip, NULL) == VM_SLOT_OK &&
          vm_slot_get(&ip) == &ddos_program_ip, "IPv4 program installs into the lwIP-hook slot");

    /* Wrong hook: the IPv4 program reads byte 9 as the protocol; on an
     * Ethernet frame byte 9 is part of the source MAC. Refused. */
    CHECK(vm_slot_install(&eth, &ddos_program_ip, &why) == VM_SLOT_ERR_CTX && why == VM_VERIFY_OK,
          "IPv4 program refused by the driver-hook slot (ctx mismatch)");
    CHECK(vm_slot_get(&eth) == &ddos_program_eth, "refused install keeps the old program");
    CHECK(vm_slot_install(&ip, &ddos_program_eth, NULL) == VM_SLOT_ERR_CTX,
          "ETH program refused by the lwIP-hook slot (ctx mismatch)");

    /* Unverifiable program: register 9 does not exist. */
    static const vm_insn_t bad[] = { { VM_OP_MOV_K, 9, 0, 0, 1 }, { VM_OP_RET, 0, 0, 0, 1 } };
    vm_program_t pbad = { bad, 2, VM_CTX_ETH };
    CHECK(vm_slot_install(&eth, &pbad, &why) == VM_SLOT_ERR_VERIFY && why == VM_VERIFY_ERR_BAD_REG,
          "unverified program refused, reason reported (bad register)");
    CHECK(vm_slot_get(&eth) == &ddos_program_eth, "failed verification keeps the old program");

    /* Invalid ctx is a verifier error, reported as such. */
    vm_program_t pnoctx = { ddos_bytecode_eth, ddos_program_eth.n_insns, 0 };
    CHECK(vm_slot_install(&eth, &pnoctx, &why) == VM_SLOT_ERR_VERIFY && why == VM_VERIFY_ERR_BAD_CTX,
          "program with ctx 0 refused by the verifier");

    CHECK(vm_slot_install(&eth, NULL, NULL) == VM_SLOT_ERR_NULL, "NULL program refused");
    CHECK(vm_slot_install(NULL, &ddos_program_eth, NULL) == VM_SLOT_ERR_NULL, "NULL slot refused");

    vm_slot_clear(&eth);
    CHECK(vm_slot_get(&eth) == NULL, "clear empties the slot");
    CHECK(strcmp(vm_slot_strerror(VM_SLOT_ERR_CTX), "program context does not match this hook") == 0,
          "strerror names the ctx error");
}

int main(void)
{
    test_isa_layout();
    test_verifier();
    test_ddos_filter();
    test_known_limitations();
    test_alu_and_branches();
    test_isa_v1_encoding();
    test_isa_v1_verifier_rules();
    test_isa_v1_unused_fields();
    test_isa_v1_alu();
    test_isa_v1_branches();
    test_isa_v1_loads();
    test_isa_v1_moves_and_ret();
    test_runtime_guards_unverified();
    test_insns_executed_all_paths();
    test_vm_slot();
    test_hooks();
    test_native();
    test_exp_configs();
    test_loader();

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
