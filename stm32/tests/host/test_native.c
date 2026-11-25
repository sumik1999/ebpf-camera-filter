/*
 * test_native.c -- differential test: the native C DDoS filter must give
 * the same verdict as the DDoS bytecode on EVERY input, otherwise the
 * VM-vs-native comparison would compare two different filters.
 *
 * Inputs (deterministic: fixed-seed PRNG, so every run checks the same
 * frames):
 *   - every length 0..64 of a set of hand-picked frames (attack, DNS,
 *     TCP to the target port, ARP, IPv6), i.e. every truncation point
 *   - 20000 structured frames (random EtherType/protocol/port mix,
 *     random truncation)
 *   - 20000 frames of pure random bytes and random length
 * For each frame both variants are compared: the ETH variant on the whole
 * frame and the IPv4 variant on the frame minus its Ethernet header.
 */
#include "test_common.h"

#include "vm_interpreter.h"
#include "ddos_bytecode.h"
#include "ddos_native.h"
#include "xdp_dma_hook.h"

static uint32_t s_rng = 0x12345678u;
static uint32_t rnd(void)            /* xorshift32 */
{
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 17;
    s_rng ^= s_rng << 5;
    return s_rng;
}

typedef struct {
    uint32_t compared, mismatches, drops, passes;
} diff_t;

static void compare(diff_t *d, const uint8_t *f, uint32_t len)
{
    int vm = vm_interpreter_run(&ddos_program_eth, f, len, NULL);
    int nat = ddos_native_eth(f, len);
    d->compared++;
    if (vm != nat) {
        if (d->mismatches++ < 5) printf("  mismatch ETH len=%u vm=%d native=%d\n", (unsigned)len, vm, nat);
    }
    (vm == VM_VERDICT_DROP) ? d->drops++ : d->passes++;

    if (len >= ETH_HDR) {
        vm = vm_interpreter_run(&ddos_program_ip, f + ETH_HDR, len - ETH_HDR, NULL);
        nat = ddos_native_ip(f + ETH_HDR, len - ETH_HDR);
        d->compared++;
        if (vm != nat) {
            if (d->mismatches++ < 5) printf("  mismatch IP len=%u vm=%d native=%d\n", (unsigned)len, vm, nat);
        }
        (vm == VM_VERDICT_DROP) ? d->drops++ : d->passes++;
    }
}

void test_native(void)
{
    uint8_t f[256];
    diff_t d = { 0, 0, 0, 0 };
    printf("[native] differential test: native C filter vs DDoS bytecode\n");

    /* 1. Every truncation point of hand-picked frames. */
    const frame_spec_t picks[] = {
        { 0x0800, 5, 17, 0x4000, 40000, DDOS_TARGET_PORT, 32 },  /* attack            */
        { 0x0800, 5, 17, 0x4000, 40000, 53, 32 },                /* DNS               */
        { 0x0800, 5, 6,  0x4000, 40000, DDOS_TARGET_PORT, 32 },  /* TCP, target port  */
        { 0x0800, 5, 1,  0x0000, 0, 0, 32 },                     /* ICMP              */
        { 0x0806, 0, 0,  0, 0, 0, 0 },                           /* ARP               */
        { 0x86DD, 0, 0,  0, 0, 0, 0 },                           /* IPv6              */
    };
    for (unsigned i = 0; i < sizeof picks / sizeof picks[0]; i++) {
        build_frame(f, sizeof f, &picks[i]);
        for (uint32_t len = 0; len <= 64; len++) {
            compare(&d, f, len);
        }
    }
    uint32_t after_picks = d.compared;

    /* 2. Structured random frames. */
    static const uint16_t etypes[] = { 0x0800, 0x0800, 0x0800, 0x0806, 0x86DD, 0x8100 };
    static const uint8_t  protos[] = { 17, 17, 17, 6, 1 };
    for (int i = 0; i < 20000; i++) {
        frame_spec_t s = { etypes[rnd() % 6], 5, protos[rnd() % 5], 0x4000,
                           (uint16_t)rnd(), (rnd() & 1) ? DDOS_TARGET_PORT : (uint16_t)rnd(),
                           (uint16_t)(rnd() % 64) };
        uint32_t n = build_frame(f, sizeof f, &s);
        if (rnd() & 1) {
            n = rnd() % (n + 1);                 /* truncate half of them */
        }
        compare(&d, f, n);
    }

    /* 3. Pure random bytes. */
    for (int i = 0; i < 20000; i++) {
        uint32_t n = rnd() % 80;
        for (uint32_t k = 0; k < n; k++) f[k] = (uint8_t)rnd();
        if (rnd() & 1) { f[12] = 0x08; f[13] = 0x00; }   /* make IPv4 likely */
        if (rnd() & 1) { f[23] = 17; f[9] = 17; }        /* make UDP likely  */
        compare(&d, f, n);
    }

    /* 4. NULL frame. */
    CHECK(ddos_native_eth(NULL, 64) == VM_VERDICT_DROP && ddos_native_ip(NULL, 64) == VM_VERDICT_DROP &&
          vm_interpreter_run(&ddos_program_eth, NULL, 64, NULL) == VM_VERDICT_DROP,
          "NULL frame: native and VM both DROP");

    printf("  compared %u verdict pairs (%u from truncation sweeps): %u DROP, %u PASS, %u mismatches\n",
           (unsigned)d.compared, (unsigned)after_picks, (unsigned)d.drops, (unsigned)d.passes,
           (unsigned)d.mismatches);
    CHECK(d.mismatches == 0, "native C and bytecode verdicts identical on every frame");
    CHECK(d.compared > 60000 && d.drops > 1000 && d.passes > 1000,
          "differential test covered both verdicts many times");

    /* 5. Through the driver hook: native filter installed as config C3. */
    frame_spec_t attack = { 0x0800, 5, 17, 0x4000, 40000, DDOS_TARGET_PORT, 32 };
    uint16_t n = (uint16_t)build_frame(f, sizeof f, &attack);
    xdp_hook_init();
    xdp_hook_install_native(ddos_native_eth);
    CHECK(xdp_hook_run(f, n) == VM_VERDICT_DROP && xdp_hook_get_stats()->rx_dropped == 1,
          "native filter at the driver hook drops the attack frame");
    xdp_hook_clear();
}
