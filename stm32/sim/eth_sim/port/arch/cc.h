/* arch/cc.h -- lwIP compiler/platform settings for the simulation
 * (Mac host build and the QEMU Cortex-M33 build). Our own file: ST's
 * version includes the STM32 "cpu.h". */
#ifndef SIM_ARCH_CC_H
#define SIM_ARCH_CC_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#if !defined(SIM_RTOS) || !SIM_RTOS
typedef int sys_prot_t;             /* the FreeRTOS build gets it from lwIP's FreeRTOS port */
#endif

#ifdef __APPLE__
/* Mac host build: the system headers already define htons() etc. (same
 * meaning); tell lwIP not to define them again. */
#define LWIP_DONT_PROVIDE_BYTEORDER_FUNCTIONS
#endif


#define PACK_STRUCT_BEGIN
#define PACK_STRUCT_STRUCT __attribute__((__packed__))
#define PACK_STRUCT_END
#define PACK_STRUCT_FIELD(x) x

void sim_fatal(const char *why);   /* sim_main.c: print and stop the run (exit code 4) */

#define LWIP_PLATFORM_DIAG(x) do { printf x; } while (0)
#define LWIP_PLATFORM_ASSERT(x) sim_fatal(x)

/* Deterministic, and no malloc: newlib's rand() allocates its state on
 * first use, which fails inside a FreeRTOS task (sim_common.c). */
uint32_t sim_rand(void);
#define LWIP_RAND() sim_rand()

#endif /* SIM_ARCH_CC_H */
