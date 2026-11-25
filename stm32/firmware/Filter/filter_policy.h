#ifndef FILTER_POLICY_H
#define FILTER_POLICY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FILTER_MAX_VLANS    4u
#define FILTER_MAX_SERVICES 8u

typedef struct {
    uint8_t protocol; /* IP protocol number: normally TCP (6) or UDP (17) */
    uint8_t reserved;
    uint16_t port;    /* destination port in host byte order */
} filter_service_t;

/* Fixed-size stateless policy. A zero-initialized policy is monitor-only and
 * passes every frame, preserving the existing C0-C4 experiment semantics. */
typedef struct {
    uint8_t drop_malformed;
    uint8_t drop_unsupported;
    uint8_t drop_ipv6;
    uint8_t drop_fragments;

    uint8_t enforce_vlan_allowlist;
    uint8_t allow_untagged;
    uint8_t vlan_count;
    uint8_t enforce_protocol_allowlist;

    uint8_t allow_arp;
    uint8_t allow_icmp;
    uint8_t allow_tcp;
    uint8_t allow_udp;
    uint8_t allow_other_l2;
    uint8_t allow_other_ipv4;
    uint8_t enforce_service_allowlist;
    uint8_t service_count;

    uint16_t allowed_vlans[FILTER_MAX_VLANS];
    filter_service_t services[FILTER_MAX_SERVICES];
} filter_policy_t;

/* Monitor policy: parser and counters run, but all traffic passes. */
void filter_policy_monitor(filter_policy_t *policy);

/* Validate array counts and service protocol/port values. */
int filter_policy_validate(const filter_policy_t *policy);

#ifdef __cplusplus
}
#endif

#endif /* FILTER_POLICY_H */
