#ifndef SIM_H
#define SIM_H

/*
 * sim.h -- helpers for programs that run on the emulated Cortex-M33.
 *
 * Instruction counting: the code to measure is placed between
 * sim_mark_begin() and sim_mark_end(). QEMU logs every executed
 * instruction (-d exec,nochain with one instruction per block), and
 * tools count_insns.py counts the instructions between the two markers.
 * The cost of the markers themselves is removed by also measuring an
 * empty window (see bench_insns.c).
 */

void sim_exit(int code);            /* end QEMU with this exit code */
int  sim_get_cmdline(char *buf, int len);   /* QEMU -semihosting-config arg=... */

/* noinline + an empty asm keep each marker a real, separate call. */
__attribute__((noinline)) void sim_mark_begin(void);
__attribute__((noinline)) void sim_mark_end(void);

#endif /* SIM_H */
