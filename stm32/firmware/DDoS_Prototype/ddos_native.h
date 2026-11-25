#ifndef DDOS_NATIVE_H
#define DDOS_NATIVE_H

/*
 * ddos_native -- the same DDoS filter as ddos_bytecode.h, written directly
 * in C. It is the baseline for the "interpretation overhead" comparison
 * (configs C1 and C3 in docs/EVALUATION_PLAN.md).
 *
 * It must give EXACTLY the same verdict as the bytecode on every input,
 * including short frames, or the comparison would be unfair. So it checks
 * the length lazily, at the same points the VM would fault:
 *
 *   step                       VM instruction        native check
 *   read EtherType (12..13)    LDPKT_H off 12        len < 14  -> DROP
 *   not IPv4                   JNE_K 0x0800          PASS
 *   read protocol (23)         LDPKT_B off 23        len < 24  -> DROP
 *   not UDP                    JNE_K 17              PASS
 *   read dst port (36..37)     LDPKT_H off 36        len < 38  -> DROP
 *   port == target             RET 0 / RET 1         DROP / PASS
 *
 * The host differential test (tests/host/test_native.c) checks this on
 * thousands of random and truncated frames.
 *
 * Same signature as hook_native_fn, so a hook can run it directly.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* pkt[0] = destination MAC (driver hook, like ddos_program_eth). */
int ddos_native_eth(const uint8_t *pkt, uint32_t len);

/* pkt[0] = IPv4 header (lwIP hook, like ddos_program_ip). */
int ddos_native_ip(const uint8_t *pkt, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* DDOS_NATIVE_H */
