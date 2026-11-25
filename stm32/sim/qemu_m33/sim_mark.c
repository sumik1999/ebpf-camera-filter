/* sim_mark.c -- instruction-count markers (see sim.h). Kept in their own
 * file so the compiler can never inline or merge them. */
#include "sim.h"

__attribute__((noinline)) void sim_mark_begin(void) { __asm__ volatile("" ::: "memory"); }
__attribute__((noinline)) void sim_mark_end(void)   { __asm__ volatile("" ::: "memory"); }
