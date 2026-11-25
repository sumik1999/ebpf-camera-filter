#ifndef EXP_CONFIG_H
#define EXP_CONFIG_H

/*
 * exp_config.h -- which experiment configuration the firmware runs
 * (docs/EVALUATION_PLAN.md). Choose at build time with -DEXP_CONFIG=<n>.
 *
 *   n  filter        where                           question it answers
 *   0  none          --                              baseline under flood
 *   1  native C      lwIP hook  (ip4_input)          late, not interpreted
 *   2  VM bytecode   lwIP hook  (ip4_input)          late, interpreted
 *   3  native C      driver hook (RxLinkCallback)    early, not interpreted
 *   4  VM bytecode   driver hook (RxLinkCallback)    the proposed system
 *
 *   C1 vs C3: effect of WHERE we filter (placement).
 *   C3 vs C4: cost of interpreting bytecode instead of native C.
 *
 * C5 (MAC hardware filter) and C6 (VM in the ISR) need the real board, so
 * they are not available in the simulation (stated as a limitation).
 *
 * In every config only ONE hook filters; the other has no filter.
 */

#define EXP_C0_NONE          0
#define EXP_C1_NATIVE_LWIP   1
#define EXP_C2_VM_LWIP       2
#define EXP_C3_NATIVE_DRIVER 3
#define EXP_C4_VM_DRIVER     4
#define EXP_NUM_CONFIGS      5

#ifndef EXP_CONFIG
#define EXP_CONFIG EXP_C4_VM_DRIVER   /* default: the proposed system */
#endif

#if EXP_CONFIG < 0 || EXP_CONFIG >= EXP_NUM_CONFIGS
#error "EXP_CONFIG must be 0..4 (C5 and C6 need hardware)"
#endif

#endif /* EXP_CONFIG_H */
