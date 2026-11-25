/*
 * rtos_main.c -- the simulated firmware WITH FreeRTOS 11.2.0 (plan step 9c),
 * on QEMU mps2-an505 (Cortex-M33). Same arguments and RESULT line as the
 * no-RTOS runner (sim/eth_sim/sim_main.c).
 *
 * Tasks and interrupt, mirroring ST's example:
 *   ETH_IRQHandler (real NVIC interrupt) -> HAL_ETH_RxCpltCallback()
 *        -> xSemaphoreGiveFromISR(RX semaphore), yield
 *   RX task "EthIf"   priority 48 (osPriorityRealtime), 1024-byte stack:
 *        take semaphore -> drain the ring (ST's or the fixed loop) -> early
 *        hook in RxLinkCallback -> tcpip_input() posts to lwIP's mailbox
 *   tcpip_thread      priority 40 (osPriorityHigh), 2048-byte stack:
 *        lwIP (ethernet_input, ip4_input + late hook, UDP echo, control port)
 *   feeder            priority 1 (test harness, plays the network): lets the
 *        simulated DMA store `burst` frames, then raises the interrupt.
 *
 * Because the feeder has the lowest priority (above idle), it runs again
 * only when the interrupt, the RX task and tcpip_thread have all finished.
 * measure=1 wraps "raise interrupt ... feeder runs again" in the
 * instruction-count markers: the whole cost of one frame, including the
 * interrupt, both task switches and the mailbox hand-off.
 *
 * Run QEMU with -icount so SysTick (1 kHz) is tied to instruction counts
 * and every run is identical.
 */
#include <stdio.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include "lwip/tcpip.h"
#include "lwip/netif.h"

#include "sim_common.h"
#include "sim_app.h"
#include "ethernetif_sim.h"
#include "ddos_prototype.h"
#include "sim.h"

#define SIM_ETH_IRQ      20u     /* same number as in startup.c's vector table */
#define PRIO_RX          48      /* osPriorityRealtime in ST's CMSIS-RTOS2 numbering */
#define PRIO_FEEDER      1

/* NVIC registers (Cortex-M33) */
#define NVIC_ISER(n)  (*(volatile uint32_t *)(0xE000E100u + 4u * (n)))
#define NVIC_ISPR(n)  (*(volatile uint32_t *)(0xE000E200u + 4u * (n)))
#define NVIC_IPR(n)   (*(volatile uint8_t  *)(0xE000E400u + (n)))

static sim_args_t s_args;
static sim_trace_t s_trace;     /* opened in main(): newlib allocates FILE buffers with malloc,
                                   which only works before the scheduler starts */
static struct netif s_netif;
static SemaphoreHandle_t s_lwip_ready;
static uint32_t s_stranded_events, s_stranded_frames;

/* ---- hooks FreeRTOS calls when something is wrong (exit codes 5-7) ---- */
void rtos_assert_failed(const char *file, int line)
{
    taskDISABLE_INTERRUPTS();
    printf("FATAL: configASSERT failed at %s:%d\n", file, line);
    sim_exit_code(5);
}
void vApplicationStackOverflowHook(TaskHandle_t t, char *name)
{
    (void)t;
    printf("FATAL: stack overflow in task %s\n", name);
    sim_exit_code(6);
}
void vApplicationMallocFailedHook(void)
{
    printf("FATAL: FreeRTOS heap exhausted\n");
    sim_exit_code(7);
}

/* ---- the Ethernet RX interrupt (stand-in for ETH_IRQn) ---- */
void ETH_IRQHandler(void)
{
    HAL_ETH_RxCpltCallback(&EthHandle);
}

static void raise_eth_irq(void)
{
    NVIC_ISPR(SIM_ETH_IRQ / 32u) = 1u << (SIM_ETH_IRQ % 32u);
    __asm__ volatile("dsb\n isb" ::: "memory");
}

/* ---- RX task: ST's ethernetif_input() ---- */
static void rx_task(void *arg)
{
    (void)arg;
    for (;;) {
        if (xSemaphoreTake(g_rx_sem, portMAX_DELAY) == pdTRUE) {
            ethernetif_rx_drain(&s_netif, s_args.loop);
        }
    }
}

/* ---- runs in tcpip_thread once lwIP is up ---- */
static void lwip_ready(void *arg)
{
    (void)arg;
    ip4_addr_t ip, mask, gw;
    IP4_ADDR(&ip, 192, 168, 10, 2);
    IP4_ADDR(&mask, 255, 255, 255, 0);
    IP4_ADDR(&gw, 192, 168, 10, 1);
    netif_add(&s_netif, &ip, &mask, &gw, NULL, ethernetif_init, tcpip_input);
    netif_set_default(&s_netif);
    netif_set_up(&s_netif);
    netif_set_link_up(&s_netif);
    sim_app_init();
    xSemaphoreGive(s_lwip_ready);
}

/* ---- feeder: plays the network, then reports ---- */
static void feeder_task(void *arg)
{
    sim_trace_t *const t = &s_trace;
    (void)arg;
    s_lwip_ready = xSemaphoreCreateBinary();
    tcpip_init(lwip_ready, NULL);
    xSemaphoreTake(s_lwip_ready, portMAX_DELAY);

    if (xTaskCreate(rx_task, "EthIf", 1024 / sizeof(StackType_t), NULL, PRIO_RX, NULL) != pdPASS)
        sim_fatal("RX task");
    NVIC_IPR(SIM_ETH_IRQ) = (uint8_t)(5u << (8 - configPRIO_BITS));   /* below MAX_SYSCALL */
    NVIC_ISER(SIM_ETH_IRQ / 32u) = 1u << (SIM_ETH_IRQ % 32u);

    if (s_args.measure) sim_calibration_windows();

    uint32_t len, t_ms, in_burst = 0;
    while ((len = sim_trace_next(t, &t_ms)) != 0) {
        /* The DMA stores the frame; with burst > 1 the interrupt is held back
         * until the whole burst has arrived (the CPU was busy elsewhere). */
        taskENTER_CRITICAL();
        sim_eth_wire_rx(&EthHandle, t->frame, len);
        taskEXIT_CRITICAL();
        if (++in_burst < (uint32_t)s_args.burst) continue;
        in_burst = 0;

        if (s_args.measure) sim_mark_begin();
        raise_eth_irq();                 /* ISR -> RX task -> (mailbox) -> tcpip_thread */
        if (s_args.measure) { sim_mark_end(); printf("CASE f%u 0\n", (unsigned)(t->n - 1)); }

        uint32_t left = sim_eth_frames_ready(&EthHandle);   /* D5 symptom, as in sim_main.c */
        if (left > 0u && uxSemaphoreGetCount(g_rx_sem) == 0u) {
            s_stranded_events++;
            s_stranded_frames += left;
        }
    }
    if (in_burst) raise_eth_irq();
    /* No wait needed: the feeder has the lowest priority, so when it runs
     * again the RX task and tcpip_thread have finished every frame. */

    /* Smallest free stack each task ever had (FreeRTOS high-water mark). */
    static TaskStatus_t st[8];
    UBaseType_t nt = uxTaskGetSystemState(st, 8, NULL);
    printf("RTOS,stack_free_min_bytes");
    for (UBaseType_t i = 0; i < nt; i++) {
        printf(",%s=%u", st[i].pcTaskName, (unsigned)(st[i].usStackHighWaterMark * sizeof(StackType_t)));
    }
    printf("\n");
    sim_print_result(&s_args, t->n, s_stranded_events, s_stranded_frames, sim_eth_frames_ready(&EthHandle));
    sim_exit_code(0);
}

int main(void)
{
    int argc;
    char **argv;
    sim_args_from_qemu(&argc, &argv);
    sim_parse_args(argc, argv, &s_args);
    if (ddos_prototype_init_config(s_args.cfg) != 0) sim_exit_code(2);
    sim_trace_open(&s_trace, s_args.trace);

    if (xTaskCreate(feeder_task, "feeder", 4096 / sizeof(StackType_t), NULL, PRIO_FEEDER, NULL) != pdPASS)
        sim_fatal("feeder task");
    vTaskStartScheduler();
    sim_fatal("scheduler returned");
    return 0;
}
