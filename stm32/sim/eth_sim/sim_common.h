#ifndef SIM_COMMON_H
#define SIM_COMMON_H

/* sim_common -- shared by the no-RTOS runner (sim_main.c) and the FreeRTOS
 * runner (sim/rtos/rtos_main.c): arguments, pcap reading, calibration
 * windows and the RESULT lines. */

#include <stdint.h>
#include <stdio.h>
#include "ethernetif_sim.h"

typedef struct {
    int cfg, burst, measure;
    rx_loop_t loop;
    const char *trace;
} sim_args_t;

typedef struct {
    FILE *f;
    uint32_t t0_ms, n;
    uint8_t frame[2048];
} sim_trace_t;

void sim_parse_args(int argc, char **argv, sim_args_t *a);
void sim_args_from_qemu(int *argc, char ***argv);          /* SIM_QEMU only */
void sim_trace_open(sim_trace_t *t, const char *path);
/* Next frame: returns its length (0 at the end); *t_ms = time since the first frame. */
uint32_t sim_trace_next(sim_trace_t *t, uint32_t *t_ms);
void sim_calibration_windows(void);                       /* for count_insns.py */
void sim_print_result(const sim_args_t *a, uint32_t frames, uint32_t stranded_events,
                      uint32_t stranded_frames, uint32_t stranded_end);
void sim_exit_code(int code);

#endif /* SIM_COMMON_H */
