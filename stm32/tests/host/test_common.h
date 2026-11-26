/* test_common.h -- shared by the host test files: the CHECK macro, the
 * pass/fail counters and a synthetic Ethernet/IPv4 frame builder. */
#ifndef TEST_COMMON_H
#define TEST_COMMON_H

#include <stdio.h>
#include <string.h>
#include <stdint.h>

extern int g_fail, g_pass;   /* defined in test_vm.c */


#define CHECK(cond, msg) do {                                   \
    if (cond) { g_pass++; }                                     \
    else { g_fail++; printf("  FAIL: %s (line %d)\n", msg, __LINE__); } \
} while (0)

/* ------------------------------------------------------------------ */
/* Synthetic frame builder                                             */
/* ------------------------------------------------------------------ */
#define ETH_HDR   14u
#define IP_HDR    20u
#define UDP_HDR    8u

typedef struct {
    uint16_t ethertype;    /* 0x0800 IPv4, 0x0806 ARP, 0x86DD IPv6, 0x8100 VLAN */
    uint8_t  ihl_words;    /* 5 = no options                                   */
    uint8_t  proto;        /* 17 UDP, 6 TCP, 1 ICMP                            */
    uint16_t frag_field;   /* flags+fragment offset                            */
    uint16_t sport, dport;
    uint16_t payload;      /* L4 payload bytes                                 */
} frame_spec_t;

static inline void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }

/* Returns frame length; writes a full Ethernet frame into buf. */
static inline uint32_t build_frame(uint8_t *buf, size_t cap, const frame_spec_t *s)
{
    memset(buf, 0, cap);
    memset(buf, 0xFF, 6);                         /* dst MAC */
    buf[6] = 0x02; buf[11] = 0x01;                /* src MAC */
    put16(&buf[12], s->ethertype);
    if (s->ethertype != 0x0800) {
        return 60u;                               /* min Ethernet frame (w/o FCS) */
    }
    uint8_t *ip = &buf[ETH_HDR];
    uint32_t ihl = (uint32_t)s->ihl_words * 4u;
    ip[0] = (uint8_t)(0x40 | s->ihl_words);
    put16(&ip[2], (uint16_t)(ihl + UDP_HDR + s->payload));
    put16(&ip[6], s->frag_field);
    ip[8] = 64;                                   /* TTL */
    ip[9] = s->proto;
    ip[12] = 10; ip[15] = 2;                      /* src 10.0.0.2 */
    ip[16] = 10; ip[19] = 1;                      /* dst 10.0.0.1 */
    uint8_t *l4 = ip + ihl;
    put16(&l4[0], s->sport);
    put16(&l4[2], s->dport);
    uint32_t len = ETH_HDR + ihl + UDP_HDR + s->payload;
    return len < 60u ? 60u : len;
}


void test_hooks(void);       /* test_hooks.c */
void test_native(void);      /* test_native.c */
void test_exp_configs(void); /* test_hooks.c */
void test_loader(void);      /* test_loader.c */
void test_packet_parser(void); /* test_packet_parser.c */
void test_ingress_filter(void); /* test_ingress_filter.c */
void test_dynamic_filter(void); /* test_dynamic_filter.c */
void test_filter_concurrency(void); /* test_filter_concurrency.c */

#endif /* TEST_COMMON_H */
