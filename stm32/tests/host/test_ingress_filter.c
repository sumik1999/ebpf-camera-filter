#include "test_common.h"

#include "ingress_filter.h"
#include "packet_parser.h"
#include "xdp_dma_hook.h"

#define F_ETH 14u
#define F_IP 20u
#define F_UDP 8u
#define F_TCP 20u

static uint32_t build_valid_ip(uint8_t *f, uint16_t vlan, uint8_t proto,
                               uint16_t frag, uint16_t dport)
{
    uint32_t l3 = vlan == PACKET_VLAN_NONE ? F_ETH : F_ETH + 4u;
    uint32_t l4 = proto == PACKET_IPPROTO_TCP ? F_TCP :
                  proto == PACKET_IPPROTO_ICMP ? 4u : F_UDP;
    memset(f, 0, 128);
    memset(f, 0xFF, 6);
    f[6] = 0x02; f[11] = 1;
    if (vlan == PACKET_VLAN_NONE) {
        put16(f + 12, PACKET_ETHERTYPE_IPV4);
    } else {
        put16(f + 12, PACKET_ETHERTYPE_VLAN);
        put16(f + 14, vlan);
        put16(f + 16, PACKET_ETHERTYPE_IPV4);
    }
    uint8_t *ip = f + l3;
    ip[0] = 0x45;
    put16(ip + 2, (uint16_t)(F_IP + l4));
    put16(ip + 6, frag);
    ip[8] = 64;
    ip[9] = proto;
    ip[12] = 10; ip[15] = 2;
    ip[16] = 10; ip[19] = 1;
    uint8_t *transport = ip + F_IP;
    if (proto == PACKET_IPPROTO_UDP || proto == PACKET_IPPROTO_TCP) {
        put16(transport, 40000);
        put16(transport + 2, dport);
    }
    if (proto == PACKET_IPPROTO_UDP) {
        put16(transport + 4, F_UDP);
    } else if (proto == PACKET_IPPROTO_TCP) {
        transport[12] = 0x50;
        transport[13] = 0x02;
    }
    return l3 + F_IP + l4;
}

static uint32_t build_l2(uint8_t *f, uint16_t ether_type)
{
    memset(f, 0, 64);
    memset(f, 0xFF, 6);
    put16(f + 12, ether_type);
    return 60;
}

static filter_policy_t strict_policy(void)
{
    filter_policy_t p;
    filter_policy_monitor(&p);
    p.drop_malformed = 1;
    p.drop_unsupported = 1;
    p.drop_ipv6 = 1;
    p.drop_fragments = 1;
    p.enforce_vlan_allowlist = 1;
    p.allow_untagged = 1;
    p.vlan_count = 1;
    p.allowed_vlans[0] = 100;
    p.enforce_protocol_allowlist = 1;
    p.allow_arp = 1;
    p.allow_icmp = 1;
    p.allow_tcp = 1;
    p.allow_udp = 1;
    p.enforce_service_allowlist = 1;
    p.service_count = 2;
    p.services[0] = (filter_service_t){ PACKET_IPPROTO_UDP, 0, 53 };
    p.services[1] = (filter_service_t){ PACKET_IPPROTO_TCP, 0, 443 };
    return p;
}

void test_ingress_filter(void)
{
    uint8_t f[128];
    uint32_t n;
    filter_policy_t p;

    printf("[filter] local stateless policy, not connected to the driver hook\n");

    ingress_filter_init();
    n = build_valid_ip(f, PACKET_VLAN_NONE, PACKET_IPPROTO_UDP, 0, 1900);
    CHECK(ingress_filter_run(f, n) == VM_VERDICT_PASS &&
          ingress_filter_get_stats()->rx_total == 1 &&
          ingress_filter_get_stats()->rx_passed == 1,
          "standalone monitor mode observes a frame without changing its verdict");
    CHECK(ingress_filter_run(f, 10) == VM_VERDICT_PASS &&
          ingress_filter_get_stats()->reason[FILTER_REASON_PASS] == 2,
          "standalone monitor policy observes malformed traffic but passes it");

    p = strict_policy();
    CHECK(filter_policy_validate(&p) && ingress_filter_configure(&p),
          "valid strict policy is copied into fixed storage");
    p.service_count = FILTER_MAX_SERVICES + 1u;
    CHECK(!filter_policy_validate(&p) && !ingress_filter_configure(&p) &&
          ingress_filter_get_policy()->service_count == 2,
          "oversized policy is refused and the active policy stays unchanged");
    p = strict_policy();
    p.allowed_vlans[0] = 4096;
    CHECK(!filter_policy_validate(&p), "VLAN IDs outside 0..4095 are refused");
    p = strict_policy();
    p.services[0].protocol = 1;
    CHECK(!filter_policy_validate(&p), "service entries must be TCP or UDP");
    p = strict_policy();
    p.services[0].port = 0;
    CHECK(!filter_policy_validate(&p), "service port zero is refused");

    CHECK(ingress_filter_run(f, 10) == VM_VERDICT_DROP,
          "strict policy drops malformed frames");

    n = build_valid_ip(f, PACKET_VLAN_NONE, PACKET_IPPROTO_UDP, 0, 53);
    CHECK(ingress_filter_run(f, n) == VM_VERDICT_PASS,
          "allowed untagged UDP/DNS service passes");
    n = build_valid_ip(f, 100, PACKET_IPPROTO_TCP, 0, 443);
    CHECK(ingress_filter_run(f, n) == VM_VERDICT_PASS,
          "allowed VLAN and TCP/443 service pass");
    n = build_valid_ip(f, 200, PACKET_IPPROTO_UDP, 0, 53);
    CHECK(ingress_filter_run(f, n) == VM_VERDICT_DROP,
          "unlisted VLAN is dropped");
    n = build_valid_ip(f, PACKET_VLAN_NONE, PACKET_IPPROTO_UDP, 0, 1900);
    CHECK(ingress_filter_run(f, n) == VM_VERDICT_DROP,
          "unlisted UDP service is dropped");
    n = build_valid_ip(f, PACKET_VLAN_NONE, PACKET_IPPROTO_TCP, 0, 80);
    CHECK(ingress_filter_run(f, n) == VM_VERDICT_DROP,
          "unlisted TCP service is dropped");
    n = build_valid_ip(f, PACKET_VLAN_NONE, PACKET_IPPROTO_UDP, 0x2000, 53);
    CHECK(ingress_filter_run(f, n) == VM_VERDICT_DROP,
          "first fragment is dropped by policy");
    n = build_valid_ip(f, PACKET_VLAN_NONE, 99, 0, 0);
    CHECK(ingress_filter_run(f, n) == VM_VERDICT_DROP,
          "unlisted IPv4 protocol is dropped");
    n = build_l2(f, PACKET_ETHERTYPE_ARP);
    CHECK(ingress_filter_run(f, n) == VM_VERDICT_PASS,
          "explicitly allowed ARP passes");
    n = build_valid_ip(f, PACKET_VLAN_NONE, PACKET_IPPROTO_ICMP, 0, 0);
    CHECK(ingress_filter_run(f, n) == VM_VERDICT_PASS,
          "explicitly allowed ICMP passes");
    n = build_l2(f, PACKET_ETHERTYPE_IPV6);
    CHECK(ingress_filter_run(f, n) == VM_VERDICT_DROP,
          "IPv6 is dropped explicitly");

    const ingress_filter_stats_t *strict_stats = ingress_filter_get_stats();
    CHECK(strict_stats->reason[FILTER_REASON_MALFORMED] == 1 &&
          strict_stats->reason[FILTER_REASON_VLAN] == 1 &&
          strict_stats->reason[FILTER_REASON_FRAGMENT] == 1 &&
          strict_stats->reason[FILTER_REASON_PROTOCOL] == 1 &&
          strict_stats->reason[FILTER_REASON_SERVICE] == 2 &&
          strict_stats->reason[FILTER_REASON_IPV6] == 1,
          "strict-policy drops are counted by exact reason");

    const ingress_filter_stats_t *stats = ingress_filter_get_stats();
    CHECK(stats->rx_total == stats->rx_passed + stats->rx_dropped,
          "standalone filter PASS and DROP counters account for every frame");

    /* Keep the production hook exactly on its original VM/native path until
     * the metadata-bytecode program is ready. */
    xdp_hook_init();
    n = build_valid_ip(f, PACKET_VLAN_NONE, PACKET_IPPROTO_UDP, 0, 1900);
    CHECK(ingress_filter_run(f, n) == VM_VERDICT_DROP &&
          xdp_hook_run(f, (uint16_t)n) == VM_VERDICT_PASS,
          "local strict policy is not yet connected to xdp_hook_run");
}
