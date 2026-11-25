/*
 * sim_main.c -- runs the simulated firmware WITHOUT an RTOS (lwIP NO_SYS)
 * on one packet trace. (The FreeRTOS version is sim/rtos/rtos_main.c.)
 *
 *   sim cfg=<0..4> loop=<st|fixed> burst=<n> trace=<file.pcap> [measure=1]
 *
 * For each frame of the trace (a standard .pcap file, viewable in Wireshark):
 *   1. the simulated MAC/DMA puts it into the RX ring (or misses it if the
 *      ring is full) and raises the RX interrupt;
 *   2. after every `burst` frames, the RX task's work runs once: take the
 *      semaphore and drain the ring with ST's loop or the fixed loop.
 * burst = how many frames arrive before the RX task gets the CPU. burst=1
 * means the task keeps up; larger values model faster traffic. lwIP timers
 * run on the trace's own timestamps, so a run is fully deterministic.
 *
 * measure=1: burst is forced to 1 and each RX-task run is wrapped in the
 * instruction-count markers (QEMU build), printing "CASE f<index> 0".
 */
#include <stdio.h>

#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/timeouts.h"
#include "netif/ethernet.h"

#include "sim_common.h"
#include "sim_app.h"
#include "ddos_prototype.h"
#include "sim.h"

static uint32_t s_now_ms;
u32_t sys_now(void) { return s_now_ms; }

static struct netif s_netif;
static uint32_t s_stranded_events, s_stranded_frames;

static void run_rx_task(const sim_args_t *a, uint32_t idx)
{
    if (a->measure) sim_mark_begin();
    if (RxPktSemaphore) {                         /* osSemaphoreAcquire(RxPktSemaphore) */
        RxPktSemaphore = 0;
        ethernetif_rx_drain(&s_netif, a->loop);
    }
    if (a->measure) { sim_mark_end(); printf("CASE f%u 0\n", (unsigned)idx); }

    /* D5 symptom: frames left in the ring while the task goes back to sleep.
     * (They wait for the next interrupt, i.e. the next arriving frame.) */
    uint32_t left = sim_eth_frames_ready(&EthHandle);
    if (left > 0u && !RxPktSemaphore) {
        s_stranded_events++;
        s_stranded_frames += left;
    }
}

int sim_run(int argc, char **argv)
{
    static sim_trace_t tr;
    sim_args_t a;
    sim_parse_args(argc, argv, &a);
    sim_trace_open(&tr, a.trace);

    lwip_init();
    ip4_addr_t ip, mask, gw;
    IP4_ADDR(&ip, 192, 168, 10, 2);
    IP4_ADDR(&mask, 255, 255, 255, 0);
    IP4_ADDR(&gw, 192, 168, 10, 1);
    netif_add(&s_netif, &ip, &mask, &gw, NULL, ethernetif_init, ethernet_input);
    netif_set_default(&s_netif);
    netif_set_up(&s_netif);
    netif_set_link_up(&s_netif);
    sim_app_init();
    if (ddos_prototype_init_config(a.cfg) != 0) sim_exit_code(2);
    if (a.measure) sim_calibration_windows();

    uint32_t len, t_ms, in_burst = 0;
    while ((len = sim_trace_next(&tr, &t_ms)) != 0) {
        s_now_ms = t_ms;
        sim_eth_wire_rx(&EthHandle, tr.frame, len);   /* DMA: no CPU work counted */
        if (++in_burst >= (uint32_t)a.burst) {
            run_rx_task(&a, tr.n - 1);
            in_burst = 0;
            sys_check_timeouts();
        }
    }
    if (in_burst) run_rx_task(&a, tr.n - 1);          /* the last partial burst */
    sim_print_result(&a, tr.n, s_stranded_events, s_stranded_frames, sim_eth_frames_ready(&EthHandle));
    return 0;
}

#ifdef SIM_QEMU
int main(void)                                        /* arguments via semihosting */
{
    int argc;
    char **argv;
    sim_args_from_qemu(&argc, &argv);
    return sim_run(argc, argv);
}
#else
void sim_mark_begin(void) { }
void sim_mark_end(void) { }
int main(int argc, char **argv) { return sim_run(argc, argv); }
#endif
