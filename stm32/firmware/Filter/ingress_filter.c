#include "ingress_filter.h"

#include <stddef.h>
#include <string.h>

#include "packet_parser.h"

#define FILTER_VERDICT_DROP 0
#define FILTER_VERDICT_PASS 1

static filter_policy_t s_policy;
static ingress_filter_stats_t s_stats;

static int finish(filter_reason_t reason)
{
    s_stats.reason[reason]++;
    if (reason == FILTER_REASON_PASS) {
        s_stats.rx_passed++;
        return FILTER_VERDICT_PASS;
    }
    s_stats.rx_dropped++;
    return FILTER_VERDICT_DROP;
}

static int vlan_allowed(const packet_meta_t *meta)
{
    if (!s_policy.enforce_vlan_allowlist) {
        return 1;
    }
    if (meta->vlan_depth == 0u) {
        return s_policy.allow_untagged != 0u;
    }
    /* For QinQ, enforce the customer/inner VLAN; for one tag, enforce it. */
    uint16_t vlan = meta->vlan_depth == 2u ? meta->inner_vlan_id : meta->outer_vlan_id;
    for (uint32_t i = 0; i < s_policy.vlan_count; i++) {
        if (s_policy.allowed_vlans[i] == vlan) {
            return 1;
        }
    }
    return 0;
}

static int service_allowed(const packet_meta_t *meta)
{
    if (!s_policy.enforce_service_allowlist) {
        return 1;
    }
    if (!meta->l4_present) {
        return 0;
    }
    for (uint32_t i = 0; i < s_policy.service_count; i++) {
        const filter_service_t *service = &s_policy.services[i];
        if (service->protocol == meta->protocol && service->port == meta->dst_port) {
            return 1;
        }
    }
    return 0;
}

void ingress_filter_init(void)
{
    filter_policy_monitor(&s_policy);
    memset(&s_stats, 0, sizeof(s_stats));
}

int ingress_filter_configure(const filter_policy_t *policy)
{
    if (!filter_policy_validate(policy)) {
        return 0;
    }
    s_policy = *policy;
    return 1;
}

int ingress_filter_run(const uint8_t *frame, uint32_t len)
{
    packet_meta_t meta;
    packet_parse_result_t parsed;

    s_stats.rx_total++;
    parsed = packet_parse_ethernet(frame, len, &meta);
    if (parsed == PACKET_PARSE_MALFORMED) {
        return s_policy.drop_malformed ? finish(FILTER_REASON_MALFORMED)
                                       : finish(FILTER_REASON_PASS);
    }
    if (meta.l3_type == PACKET_L3_IPV6 && s_policy.drop_ipv6) {
        return finish(FILTER_REASON_IPV6);
    }
    if (parsed == PACKET_PARSE_UNSUPPORTED && s_policy.drop_unsupported) {
        return finish(FILTER_REASON_UNSUPPORTED);
    }
    if (!vlan_allowed(&meta)) {
        return finish(FILTER_REASON_VLAN);
    }
    if (meta.fragment_state != PACKET_FRAGMENT_NONE && s_policy.drop_fragments) {
        return finish(FILTER_REASON_FRAGMENT);
    }

    if (s_policy.enforce_protocol_allowlist) {
        if (meta.l3_type == PACKET_L3_ARP) {
            return s_policy.allow_arp ? finish(FILTER_REASON_PASS)
                                      : finish(FILTER_REASON_PROTOCOL);
        }
        if (meta.l3_type != PACKET_L3_IPV4) {
            return s_policy.allow_other_l2 ? finish(FILTER_REASON_PASS)
                                           : finish(FILTER_REASON_PROTOCOL);
        }
        if (meta.protocol == PACKET_IPPROTO_ICMP) {
            return s_policy.allow_icmp ? finish(FILTER_REASON_PASS)
                                       : finish(FILTER_REASON_PROTOCOL);
        }
        if (meta.protocol == PACKET_IPPROTO_TCP && !s_policy.allow_tcp) {
            return finish(FILTER_REASON_PROTOCOL);
        }
        if (meta.protocol == PACKET_IPPROTO_UDP && !s_policy.allow_udp) {
            return finish(FILTER_REASON_PROTOCOL);
        }
        if (meta.protocol != PACKET_IPPROTO_TCP && meta.protocol != PACKET_IPPROTO_UDP &&
            !s_policy.allow_other_ipv4) {
            return finish(FILTER_REASON_PROTOCOL);
        }
    }

    if ((meta.protocol == PACKET_IPPROTO_TCP || meta.protocol == PACKET_IPPROTO_UDP) &&
        !service_allowed(&meta)) {
        return finish(FILTER_REASON_SERVICE);
    }
    return finish(FILTER_REASON_PASS);
}

const ingress_filter_stats_t *ingress_filter_get_stats(void)
{
    return &s_stats;
}

const filter_policy_t *ingress_filter_get_policy(void)
{
    return &s_policy;
}
