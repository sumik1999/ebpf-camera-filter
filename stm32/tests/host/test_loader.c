/*
 * test_loader.c -- host tests for vm_loader (rules sent to a running device
 * as data, gap G9): message checks, decoding, install through the hooks,
 * fail-safe behaviour and double buffering.
 */
#include "test_common.h"

#include "vm_loader.h"
#include "ddos_bytecode.h"
#include "xdp_dma_hook.h"
#include "lwip_ip4_hook.h"

/* Build an XVM1 message from instructions (explicit little-endian encoding). */
static uint32_t make_msg(uint8_t *m, const vm_insn_t *in, uint32_t n, uint8_t ctx)
{
    m[0] = 'X'; m[1] = 'V'; m[2] = 'M'; m[3] = '1';
    m[4] = VM_ISA_VERSION; m[5] = ctx;
    m[6] = (uint8_t)n; m[7] = (uint8_t)(n >> 8);
    uint8_t *b = m + VM_LOADER_HDR_LEN;
    for (uint32_t i = 0; i < n; i++, b += 8) {
        uint16_t off = (uint16_t)in[i].offset;
        uint32_t imm = (uint32_t)in[i].imm;
        b[0] = in[i].opcode;
        b[1] = (uint8_t)(in[i].dst_reg | (in[i].src_reg << 4));
        b[2] = (uint8_t)off; b[3] = (uint8_t)(off >> 8);
        b[4] = (uint8_t)imm; b[5] = (uint8_t)(imm >> 8); b[6] = (uint8_t)(imm >> 16); b[7] = (uint8_t)(imm >> 24);
    }
    uint32_t crc = vm_crc32(m + VM_LOADER_HDR_LEN, 8u * n);
    m[8] = (uint8_t)crc; m[9] = (uint8_t)(crc >> 8); m[10] = (uint8_t)(crc >> 16); m[11] = (uint8_t)(crc >> 24);
    return VM_LOADER_HDR_LEN + 8u * n;
}

static const vm_program_t *s_captured;
static vm_slot_result_t capture(const vm_program_t *p, vm_verify_result_t *why)
{
    if (why) *why = VM_VERIFY_OK;
    s_captured = p;
    return VM_SLOT_OK;
}

/* The DDoS filter with another target port (the loader demo blocks port 53). */
static void port_program(vm_insn_t *out, uint16_t port)
{
    memcpy(out, ddos_program_eth.insns, 8u * ddos_program_eth.n_insns);
    out[5].imm = port;
}

static int drops(uint16_t port)
{
    uint8_t f[128];
    frame_spec_t s = { 0x0800, 5, 17, 0x4000, 40000, port, 32 };
    uint32_t n = build_frame(f, sizeof f, &s);
    return xdp_hook_run(f, (uint16_t)n) == VM_VERDICT_DROP;
}

void test_loader(void)
{
    static uint8_t m[VM_LOADER_MAX_MSG + 16];
    static vm_loader_t ld_eth, ld_ip;
    vm_slot_result_t sr;
    vm_verify_result_t why;
    uint32_t n;
    printf("[loader] XVM1 messages: checks, decode, install, double buffering\n");

    CHECK(vm_crc32((const uint8_t *)"123456789", 9) == 0xCBF43926u, "CRC-32 known answer (zlib)");

    /* Decoding: the byte layout example from docs/ISA.md section 3. */
    static const uint8_t ex[] = { 'X','V','M','1', 1, VM_CTX_ETH, 1, 0, 0,0,0,0,
                                  0x13, 0x53, 0xFE, 0xFF, 0x78, 0x56, 0x34, 0x12 };
    uint8_t exm[sizeof ex];
    memcpy(exm, ex, sizeof ex);
    uint32_t c = vm_crc32(exm + 12, 8);
    exm[8] = (uint8_t)c; exm[9] = (uint8_t)(c >> 8); exm[10] = (uint8_t)(c >> 16); exm[11] = (uint8_t)(c >> 24);
    static vm_loader_t ld_cap;
    vm_loader_init(&ld_cap, VM_CTX_ETH, capture);
    CHECK(vm_loader_load(&ld_cap, exm, sizeof exm, NULL, NULL) == VM_LOAD_OK && s_captured != NULL &&
          s_captured->insns[0].opcode == VM_OP_JNE_K && s_captured->insns[0].dst_reg == 3 &&
          s_captured->insns[0].src_reg == 5 && s_captured->insns[0].offset == -2 &&
          s_captured->insns[0].imm == 0x12345678, "bytes 13 53 FE FF 78 56 34 12 decode as in ISA.md");

    /* Embedded storage boundary: one instruction and exactly VM_MAX_INSNS
     * fit; one more is rejected before any body access. */
    static vm_insn_t boundary[VM_MAX_INSNS];
    boundary[0] = (vm_insn_t){ VM_OP_RET, 0, 0, 0, VM_VERDICT_PASS };
    n = make_msg(m, boundary, 1, VM_CTX_ETH);
    CHECK(vm_loader_load(&ld_cap, m, n, NULL, NULL) == VM_LOAD_OK &&
          s_captured->n_insns == 1, "loader accepts a one-instruction program");
    for (uint32_t i = 0; i < VM_MAX_INSNS; i++)
        boundary[i] = (vm_insn_t){ VM_OP_MOV_K, 1, 0, 0, 0 };
    boundary[VM_MAX_INSNS - 1u] = (vm_insn_t){ VM_OP_RET, 0, 0, 0, VM_VERDICT_PASS };
    n = make_msg(m, boundary, VM_MAX_INSNS, VM_CTX_ETH);
    CHECK(vm_loader_load(&ld_cap, m, n, NULL, NULL) == VM_LOAD_OK &&
          s_captured->n_insns == VM_MAX_INSNS,
          "loader accepts exactly VM_MAX_INSNS instructions");
    CHECK(sizeof(ld_cap.insns) == 2u * VM_MAX_INSNS * sizeof(vm_insn_t),
          "loader has exactly two bounded instruction banks");

    /* Install through the real hooks. */
    xdp_hook_init();
    lwip_ip4_hook_init();
    vm_loader_init(&ld_eth, VM_CTX_ETH, xdp_hook_install_vm);
    vm_loader_init(&ld_ip, VM_CTX_IPV4, lwip_ip4_hook_install_vm);

    n = make_msg(m, ddos_program_eth.insns, ddos_program_eth.n_insns, VM_CTX_ETH);
    CHECK(vm_loader_msg_ctx(m, n) == VM_CTX_ETH, "message ctx can be read before loading");
    CHECK(vm_loader_load(&ld_eth, m, n, &sr, &why) == VM_LOAD_OK && xdp_hook_filter_kind() == HOOK_FILTER_VM,
          "DDoS program sent as a message is installed in the driver hook");
    CHECK(drops(DDOS_TARGET_PORT) && !drops(53), "loaded program drops port 1900, passes port 53");

    /* Every bad message is refused and the running program stays. */
    struct { const char *what; vm_load_result_t want; } bad[] = {
        { "short message", VM_LOAD_ERR_SHORT }, { "bad magic", VM_LOAD_ERR_MAGIC },
        { "ISA version 2", VM_LOAD_ERR_VERSION }, { "IPv4 program to the ETH loader", VM_LOAD_ERR_CTX },
        { "zero instructions", VM_LOAD_ERR_LENGTH }, { "VM_MAX_INSNS + 1", VM_LOAD_ERR_LENGTH },
        { "truncated by one byte", VM_LOAD_ERR_LENGTH }, { "one bit flipped", VM_LOAD_ERR_CRC },
    };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        uint32_t len = make_msg(m, ddos_program_eth.insns, ddos_program_eth.n_insns, VM_CTX_ETH);
        switch (i) {
        case 0: len = 11; break;
        case 1: m[0] = 'Y'; break;
        case 2: m[4] = 2; break;
        case 3: m[5] = VM_CTX_IPV4; break;
        case 4: m[6] = 0; m[7] = 0; break;
        case 5: {
            uint32_t over = VM_MAX_INSNS + 1u;
            m[6] = (uint8_t)over; m[7] = (uint8_t)(over >> 8);
            break;
        }
        case 6: len -= 1; break;
        case 7: m[VM_LOADER_HDR_LEN + 21] ^= 0x01; break;
        }
        char msg[96];
        snprintf(msg, sizeof msg, "%s: refused (%s), old program still running", bad[i].what,
                 vm_load_strerror(bad[i].want));
        CHECK(vm_loader_load(&ld_eth, m, len, &sr, &why) == bad[i].want &&
              drops(DDOS_TARGET_PORT) && !drops(53), msg);
    }

    /* A well-formed message whose program fails the verifier. */
    static const vm_insn_t badreg[] = { { VM_OP_MOV_K, 9, 0, 0, 1 }, { VM_OP_RET, 0, 0, 0, 1 } };
    n = make_msg(m, badreg, 2, VM_CTX_ETH);
    CHECK(vm_loader_load(&ld_eth, m, n, &sr, &why) == VM_LOAD_ERR_INSTALL && sr == VM_SLOT_ERR_VERIFY &&
          why == VM_VERIFY_ERR_BAD_REG && drops(DDOS_TARGET_PORT),
          "unverifiable program refused by the verifier; old program still running");

    /* Live update: change the blocked port without any rebuild. */
    static vm_insn_t p53[8], p1900[8], p123[8];
    port_program(p53, 53);
    port_program(p1900, DDOS_TARGET_PORT);
    port_program(p123, 123);
    n = make_msg(m, p53, 8, VM_CTX_ETH);
    CHECK(vm_loader_load(&ld_eth, m, n, NULL, NULL) == VM_LOAD_OK && drops(53) && !drops(DDOS_TARGET_PORT),
          "live update: now port 53 is dropped and port 1900 passes");

    /* Double buffering: the next load never writes into the installed buffer,
     * even when the other hook's loader is used in between. */
    const vm_insn_t *installed = ld_eth.prog[ld_eth.next ^ 1u].insns;
    vm_insn_t snapshot[8];
    memcpy(snapshot, installed, sizeof snapshot);
    n = make_msg(m, ddos_program_ip.insns, ddos_program_ip.n_insns, VM_CTX_IPV4);
    CHECK(vm_loader_load(&ld_ip, m, n, NULL, NULL) == VM_LOAD_OK && lwip_ip4_hook_filter_kind() == HOOK_FILTER_VM,
          "IPv4 program loads into the lwIP hook through its own loader");
    n = make_msg(m, badreg, 2, VM_CTX_ETH);
    (void)vm_loader_load(&ld_eth, m, n, NULL, NULL);          /* refused, writes the free buffer */
    CHECK(memcmp(snapshot, installed, sizeof snapshot) == 0 && drops(53),
          "a refused load does not touch the installed program");
    n = make_msg(m, p123, 8, VM_CTX_ETH);
    CHECK(vm_loader_load(&ld_eth, m, n, NULL, NULL) == VM_LOAD_OK && drops(123) && !drops(53) &&
          memcmp(snapshot, installed, sizeof snapshot) == 0,
          "next load goes to the other buffer: the previous program's bytes are untouched");
    n = make_msg(m, p1900, 8, VM_CTX_ETH);
    CHECK(vm_loader_load(&ld_eth, m, n, NULL, NULL) == VM_LOAD_OK && drops(DDOS_TARGET_PORT) && !drops(123),
          "buffers alternate: a third load reuses the first buffer");

    xdp_hook_init();
    lwip_ip4_hook_init();
}
