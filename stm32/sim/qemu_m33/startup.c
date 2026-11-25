/*
 * startup.c -- minimal bare-metal start-up for QEMU mps2-an505 (Cortex-M33).
 *
 * Reset: enable the FPU, initialise .data/.bss, set up semihosting I/O,
 * run main(), then end QEMU with main()'s return value as the exit code.
 *
 * Semihosting = the program asks QEMU to do I/O for it (print, exit) with
 * a `BKPT 0xAB` instruction. QEMU must be started with -semihosting.
 */
#include <stdint.h>
#include <stdio.h>
#include "sim.h"

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;
extern int main(void);
extern void initialise_monitor_handles(void);   /* newlib rdimon */

/* ARM semihosting call: r0 = operation, r1 = parameter. */
static int semihost(int op, void *arg)
{
    register int r0 __asm__("r0") = op;
    register void *r1 __asm__("r1") = arg;
    __asm__ volatile("bkpt 0xab" : "+r"(r0) : "r"(r1) : "memory");
    return r0;
}

int sim_get_cmdline(char *buf, int len)
{
    /* SYS_GET_CMDLINE (0x15): {buffer, length} -> 0 on success */
    uint32_t block[2] = { (uint32_t)buf, (uint32_t)len };
    return semihost(0x15, block);
}

void sim_exit(int code)
{
    /* SYS_EXIT_EXTENDED (0x20): {reason = ADP_Stopped_ApplicationExit, code} */
    uint32_t block[2] = { 0x20026u, (uint32_t)code };
    fflush(stdout);
    semihost(0x20, block);
    for (;;) { }
}

void Reset_Handler(void)
{
    /* The build uses hardware floating point: enable CP10/CP11 (full access)
     * before any FP instruction can run. */
    volatile uint32_t *cpacr = (volatile uint32_t *)0xE000ED88u;
    *cpacr |= (0xFu << 20);
    __asm__ volatile("dsb\n isb" ::: "memory");

    for (uint32_t *s = &_sidata, *d = &_sdata; d < &_edata; ) *d++ = *s++;
    for (uint32_t *d = &_sbss; d < &_ebss; ) *d++ = 0;

    initialise_monitor_handles();
    sim_exit(main());
}

/* Any fault ends the run with exit code 3, printing the fault status and
 * the PC/LR the CPU pushed on exception entry. The naked stub passes the
 * stack pointer before the C function can push anything on it. */
__attribute__((used)) void Fault_Report(const uint32_t *sp)
{
    printf("\nFAULT: CFSR=0x%08lx HFSR=0x%08lx BFAR=0x%08lx stacked PC=0x%08lx LR=0x%08lx\n",
           (unsigned long)*(volatile uint32_t *)0xE000ED28u,
           (unsigned long)*(volatile uint32_t *)0xE000ED2Cu,
           (unsigned long)*(volatile uint32_t *)0xE000ED38u,
           (unsigned long)sp[6], (unsigned long)sp[5]);
    sim_exit(3);
}

__attribute__((naked)) void Fault_Handler(void)
{
    __asm__ volatile("mrs r0, msp\n b Fault_Report");
}

/* Handlers that a program may provide (FreeRTOS provides SVC/PendSV/SysTick;
 * the RTOS simulation provides ETH_IRQHandler). Unused ones end the run. */
void SVC_Handler(void) __attribute__((weak, alias("Fault_Handler")));
void PendSV_Handler(void) __attribute__((weak, alias("Fault_Handler")));
void SysTick_Handler(void) __attribute__((weak, alias("Fault_Handler")));
void ETH_IRQHandler(void) __attribute__((weak, alias("Fault_Handler")));

/* External interrupt used as a stand-in for the STM32's ETH_IRQn. */
#define SIM_ETH_IRQ  20

/* Vector table: initial stack pointer, the 16 system exceptions, then
 * external interrupts 0..31. */
__attribute__((section(".vectors"), used))
static void (*const vectors[16 + 32])(void) = {
    (void (*)(void))(&_estack),
    Reset_Handler,
    Fault_Handler,      /* NMI        */
    Fault_Handler,      /* HardFault  */
    Fault_Handler,      /* MemManage  */
    Fault_Handler,      /* BusFault   */
    Fault_Handler,      /* UsageFault */
    Fault_Handler,      /* SecureFault*/
    0, 0, 0,
    SVC_Handler,
    Fault_Handler,      /* DebugMon   */
    0,
    PendSV_Handler,
    SysTick_Handler,
    [16 + SIM_ETH_IRQ] = ETH_IRQHandler,
};
