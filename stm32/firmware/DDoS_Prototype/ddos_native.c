/*
 * ddos_native.c -- native C version of the DDoS filter. See ddos_native.h
 * for why every length check sits exactly where the VM would fault.
 */
#include "ddos_native.h"
#include "ddos_bytecode.h"   /* DDOS_TARGET_PORT, VM_VERDICT_* */

/* Big-endian 16-bit read, like LDPKT_H. */
static inline uint32_t be16(const uint8_t *p)
{
    return ((uint32_t)p[0] << 8) | (uint32_t)p[1];
}

int ddos_native_eth(const uint8_t *pkt, uint32_t len)
{
    if (pkt == 0 || len < 14u)      return VM_VERDICT_DROP;   /* can't read EtherType */
    if (be16(pkt + 12) != 0x0800u)  return VM_VERDICT_PASS;   /* not IPv4             */
    if (len < 24u)                  return VM_VERDICT_DROP;   /* can't read protocol  */
    if (pkt[23] != 17u)             return VM_VERDICT_PASS;   /* not UDP              */
    if (len < 38u)                  return VM_VERDICT_DROP;   /* can't read dst port  */
    return (be16(pkt + 36) == DDOS_TARGET_PORT) ? VM_VERDICT_DROP : VM_VERDICT_PASS;
}

int ddos_native_ip(const uint8_t *pkt, uint32_t len)
{
    if (pkt == 0 || len < 10u)      return VM_VERDICT_DROP;   /* can't read protocol  */
    if (pkt[9] != 17u)              return VM_VERDICT_PASS;   /* not UDP              */
    if (len < 24u)                  return VM_VERDICT_DROP;   /* can't read dst port  */
    return (be16(pkt + 22) == DDOS_TARGET_PORT) ? VM_VERDICT_DROP : VM_VERDICT_PASS;
}
