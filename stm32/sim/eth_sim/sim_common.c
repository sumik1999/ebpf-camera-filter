/* sim_common.c -- see sim_common.h. */
#include <stdlib.h>
#include <string.h>

#include "sim_common.h"
#include "sim_app.h"
#include "xdp_dma_hook.h"
#include "lwip_ip4_hook.h"
#include "sim.h"

uint32_t sim_rand(void)                 /* xorshift32, fixed seed */
{
    static uint32_t x = 0x2545F491u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return x;
}

void sim_exit_code(int code)
{
#ifdef SIM_QEMU
    sim_exit(code);
#else
    exit(code);
#endif
    for (;;) { }
}

void sim_fatal(const char *why)
{
    printf("FATAL: %s\n", why);
    sim_exit_code(4);
}

void sim_parse_args(int argc, char **argv, sim_args_t *a)
{
    a->cfg = 4; a->burst = 1; a->measure = 0; a->loop = RX_LOOP_FIXED; a->trace = NULL;
    for (int i = 1; i < argc; i++) {
        const char *s = argv[i];
        if (strncmp(s, "cfg=", 4) == 0)          a->cfg = atoi(s + 4);
        else if (strncmp(s, "burst=", 6) == 0)   a->burst = atoi(s + 6);
        else if (strncmp(s, "measure=", 8) == 0) a->measure = atoi(s + 8);
        else if (strcmp(s, "loop=st") == 0)      a->loop = RX_LOOP_ST;
        else if (strcmp(s, "loop=fixed") == 0)   a->loop = RX_LOOP_FIXED;
        else if (strncmp(s, "trace=", 6) == 0)   a->trace = s + 6;
        else { printf("unknown argument: %s\n", s); sim_exit_code(2); }
    }
    if (a->burst < 1 || a->trace == NULL) { printf("need trace= and burst>=1\n"); sim_exit_code(2); }
    if (a->measure) a->burst = 1;
}

#ifdef SIM_QEMU
void sim_args_from_qemu(int *argc, char ***argv)
{
    static char cmd[512];
    static char *v[16];
    int n = 0;
    if (sim_get_cmdline(cmd, sizeof cmd) != 0) sim_fatal("no command line");
    for (char *t = strtok(cmd, " "); t != NULL && n < 16; t = strtok(NULL, " ")) v[n++] = t;
    *argc = n;
    *argv = v;
}
#endif

static uint32_t rd32(const uint8_t *b) { return b[0] | (b[1] << 8) | (b[2] << 16) | ((uint32_t)b[3] << 24); }

void sim_trace_open(sim_trace_t *t, const char *path)
{
    uint8_t gh[24];
    t->f = fopen(path, "rb");
    t->n = 0;
    t->t0_ms = 0;
    if (t->f == NULL || fread(gh, 1, 24, t->f) != 24 || rd32(gh) != 0xA1B2C3D4u) {
        printf("cannot read pcap %s\n", path);
        sim_exit_code(2);
    }
}

uint32_t sim_trace_next(sim_trace_t *t, uint32_t *t_ms)
{
    uint8_t rh[16];
    if (fread(rh, 1, 16, t->f) != 16) {
        fclose(t->f);
        return 0;
    }
    uint32_t ts_ms = rd32(rh) * 1000u + rd32(rh + 4) / 1000u;
    uint32_t len = rd32(rh + 8);
    if (len == 0 || len > sizeof t->frame || fread(t->frame, 1, len, t->f) != len) sim_fatal("bad pcap record");
    if (t->n++ == 0) t->t0_ms = ts_ms;
    *t_ms = ts_ms - t->t0_ms;
    return len;
}

void sim_calibration_windows(void)
{
    /* Marker overhead, and exactly 10 NOPs that must measure 10. */
    sim_mark_begin();
    sim_mark_end();
    printf("CASE calib_empty 0\n");
    sim_mark_begin();
    __asm__ volatile("nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop");
    sim_mark_end();
    printf("CASE calib_nop10 0\n");
}

void sim_print_result(const sim_args_t *a, uint32_t n, uint32_t stranded_events,
                      uint32_t stranded_frames, uint32_t stranded_end)
{
    const hook_stats_t *e = xdp_hook_get_stats(), *l = lwip_ip4_hook_get_stats();
    printf("RESULT,cfg,loop,burst,frames_in,mac_missed,early_total,early_pass,early_drop,"
           "late_total,late_pass,late_drop,delivered,echo_rx,tx_echo,tx_ping_reply,tx_icmp_unreach,"
           "tx_arp,tx_other,stranded_events,stranded_frames,stranded_at_end,rx_alloc_errors,pool_max,"
           "ctrl_ok,ctrl_refused,tx_ctrl\n");
    printf("RESULT,%d,%s,%d,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n",
           a->cfg, a->loop == RX_LOOP_ST ? "st" : "fixed", a->burst, (unsigned)n,
           (unsigned)EthHandle.missed_frames,
           (unsigned)e->rx_total, (unsigned)(e->rx_passed + e->rx_no_filter_pass), (unsigned)e->rx_dropped,
           (unsigned)l->rx_total, (unsigned)(l->rx_passed + l->rx_no_filter_pass), (unsigned)l->rx_dropped,
           (unsigned)g_ethif.delivered, (unsigned)g_echo_rx, (unsigned)g_ethif.tx_echo,
           (unsigned)g_ethif.tx_ping_reply, (unsigned)g_ethif.tx_icmp_unreach, (unsigned)g_ethif.tx_arp,
           (unsigned)g_ethif.tx_other, (unsigned)stranded_events, (unsigned)stranded_frames,
           (unsigned)stranded_end, (unsigned)g_ethif.rx_alloc_errors, (unsigned)g_ethif.pool_in_use_max,
           (unsigned)g_ctrl_ok, (unsigned)g_ctrl_refused, (unsigned)g_ethif.tx_ctrl);
}
