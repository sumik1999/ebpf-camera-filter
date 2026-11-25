/*
 * bench_insns.c -- instructions executed per packet, on the emulated
 * Cortex-M33, for every filter path we compare.
 *
 * Each case runs ONE call between sim_mark_begin() and sim_mark_end().
 * After the window it prints "CASE <name> <verdict>", so count_insns.py
 * can match the N-th window in the QEMU trace to the N-th name.
 *
 * Two calibration cases check the method itself:
 *   calib_empty   nothing between the markers (marker overhead, subtracted
 *                 from every other case)
 *   calib_nop10   exactly 10 NOP instructions (must measure 10)
 *
 * What is measured: instructions executed, from the call to the filter to
 * its return (argument setup and the call included). This is a LOWER
 * BOUND on CPU cycles: on real hardware loads, branches and flash wait
 * states take extra cycles, which QEMU does not model.
 */
#include <stdio.h>
#include <string.h>

#include "sim.h"
#include "test_common.h"        /* frame_spec_t, build_frame */
#include "vm_interpreter.h"
#include "ddos_bytecode.h"
#include "ddos_native.h"
#include "ddos_prototype.h"
#include "exp_config.h"
#include "xdp_dma_hook.h"
#include "lwip_ip4_hook.h"

int g_fail, g_pass;             /* test_common.h declares these */
static volatile int g_sink;

#define MEASURE(name, expr) do {                     \
        sim_mark_begin();                            \
        g_sink = (expr);                             \
        sim_mark_end();                              \
        printf("CASE %s %d\n", (name), g_sink);      \
    } while (0)

typedef struct {
    const char *name;
    uint8_t buf[128];
    uint32_t len;
} pkt_t;

static pkt_t P[5];

static void make(pkt_t *p, const char *name, frame_spec_t s, uint32_t trunc)
{
    p->name = name;
    p->len = build_frame(p->buf, sizeof p->buf, &s);
    if (trunc) p->len = trunc;
}

int main(void)
{
    make(&P[0], "attack", (frame_spec_t){ 0x0800, 5, 17, 0x4000, 40000, DDOS_TARGET_PORT, 32 }, 0);
    make(&P[1], "udp_other", (frame_spec_t){ 0x0800, 5, 17, 0x4000, 40000, 53, 32 }, 0);
    make(&P[2], "tcp", (frame_spec_t){ 0x0800, 5, 6, 0x4000, 40000, DDOS_TARGET_PORT, 32 }, 0);
    make(&P[3], "arp", (frame_spec_t){ 0x0806, 0, 0, 0, 0, 0, 0 }, 0);
    make(&P[4], "runt20", (frame_spec_t){ 0x0800, 5, 17, 0x4000, 40000, DDOS_TARGET_PORT, 32 }, 20);
    char name[64];

    printf("BENCH start\n");

    /* Calibration */
    MEASURE("calib_empty", 0);
    sim_mark_begin();
    __asm__ volatile("nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop");
    g_sink = 0;
    sim_mark_end();
    printf("CASE calib_nop10 0\n");

    /* Filter only: the VM interpreter vs the native C function. */
    for (int i = 0; i < 5; i++) {
        snprintf(name, sizeof name, "filter_vm_eth_%s", P[i].name);
        MEASURE(name, vm_interpreter_run(&ddos_program_eth, P[i].buf, P[i].len, NULL));
        snprintf(name, sizeof name, "filter_native_eth_%s", P[i].name);
        MEASURE(name, ddos_native_eth(P[i].buf, P[i].len));
    }
    for (int i = 0; i < 3; i++) {   /* IPv4 variant: lwIP hook sees no Ethernet header */
        snprintf(name, sizeof name, "filter_vm_ip_%s", P[i].name);
        MEASURE(name, vm_interpreter_run(&ddos_program_ip, P[i].buf + 14, P[i].len - 14, NULL));
        snprintf(name, sizeof name, "filter_native_ip_%s", P[i].name);
        MEASURE(name, ddos_native_ip(P[i].buf + 14, P[i].len - 14));
    }

    /* Whole hook call per experiment config (hook bookkeeping included).
     * Driver-hook configs use xdp_hook_run(); lwIP-hook configs use the
     * lwIP hook's filter core (the pbuf glue adds a few instructions). */
    for (int c = 0; c < EXP_NUM_CONFIGS; c++) {
        ddos_prototype_init_config(c);
        int at_lwip = (c == EXP_C1_NATIVE_LWIP || c == EXP_C2_VM_LWIP);
        for (int i = 0; i < 4; i++) {
            snprintf(name, sizeof name, "hook_C%d_%s", c, P[i].name);
            if (at_lwip) {
                MEASURE(name, lwip_ip4_hook_run(P[i].buf + 14, P[i].len - 14));
            } else {
                MEASURE(name, xdp_hook_run(P[i].buf, (uint16_t)P[i].len));
            }
        }
    }

    printf("BENCH end\n");
    return 0;
}
