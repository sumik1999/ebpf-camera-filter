#include "test_common.h"

#include "packet_parser.h"

#define T_ETH 14u
#define T_IP 20u
#define T_UDP 8u
#define T_TCP 20u

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)v;
}

static uint32_t make_ipv4(uint8_t *f, uint32_t cap, uint8_t vlan_depth,
                          uint8_t ihl_words, uint8_t proto, uint16_t frag,
                          uint16_t sport, uint16_t dport)
{
    uint32_t l3 = T_ETH + 4u * vlan_depth;
    uint32_t ihl = 4u * ihl_words;
    uint32_t l4 = proto == PACKET_IPPROTO_TCP ? T_TCP :
                  proto == PACKET_IPPROTO_ICMP ? 4u : T_UDP;
    uint32_t total = l3 + ihl + l4;
    if (cap < total) return 0;
    memset(f, 0, cap);
    memset(f, 0xFF, 6);
    f[6] = 0x02; f[11] = 0x01;

    if (vlan_depth == 0u) {
        put16(f + 12, PACKET_ETHERTYPE_IPV4);
    } else {
        put16(f + 12, vlan_depth == 2u ? PACKET_ETHERTYPE_QINQ : PACKET_ETHERTYPE_VLAN);
        put16(f + 14, 100u);
        put16(f + 16, vlan_depth == 2u ? PACKET_ETHERTYPE_VLAN : PACKET_ETHERTYPE_IPV4);
        if (vlan_depth == 2u) {
            put16(f + 18, 200u);
            put16(f + 20, PACKET_ETHERTYPE_IPV4);
        }
    }

    uint8_t *ip = f + l3;
    ip[0] = (uint8_t)(0x40u | ihl_words);
    put16(ip + 2, (uint16_t)(ihl + l4));
    put16(ip + 6, frag);
    ip[8] = 64;
    ip[9] = proto;
    put32(ip + 12, 0x0A000002u);
    put32(ip + 16, 0x0A000001u);

    uint8_t *transport = ip + ihl;
    if (proto == PACKET_IPPROTO_UDP || proto == PACKET_IPPROTO_TCP) {
        put16(transport, sport);
        put16(transport + 2, dport);
    }
    if (proto == PACKET_IPPROTO_UDP) {
        put16(transport + 4, T_UDP);
    } else if (proto == PACKET_IPPROTO_TCP) {
        transport[12] = 0x50;
        transport[13] = 0x02; /* SYN */
    }
    return total;
}

void test_packet_parser(void)
{
    uint8_t f[256];
    packet_meta_t m;
    uint32_t n;

    printf("[parser] normalized Ethernet/VLAN/IPv4/L4 metadata\n");
    CHECK(sizeof(packet_meta_t) <= 40u, "packet metadata remains bounded (<= 40 bytes)");
    CHECK(packet_parse_ethernet(NULL, 0, &m) == PACKET_PARSE_MALFORMED && m.malformed,
          "NULL frame is malformed");
    CHECK(packet_parse_ethernet(f, sizeof f, NULL) == PACKET_PARSE_MALFORMED,
          "NULL metadata output is refused");

    memset(f, 0, sizeof f);
    for (uint32_t len = 0; len < T_ETH; len++) {
        CHECK(packet_parse_ethernet(f, len, &m) == PACKET_PARSE_MALFORMED,
              "every truncated Ethernet header is malformed");
    }

    memset(f, 0, sizeof f);
    f[0] = 0x01;
    put16(f + 12, PACKET_ETHERTYPE_ARP);
    CHECK(packet_parse_ethernet(f, 60, &m) == PACKET_PARSE_OK &&
          m.l3_type == PACKET_L3_ARP && m.multicast && !m.broadcast,
          "ARP and multicast destination are classified");
    memset(f, 0xFF, 6);
    CHECK(packet_parse_ethernet(f, 60, &m) == PACKET_PARSE_OK && m.broadcast,
          "broadcast destination is identified");

    put16(f + 12, PACKET_ETHERTYPE_IPV6);
    CHECK(packet_parse_ethernet(f, 60, &m) == PACKET_PARSE_UNSUPPORTED &&
          m.l3_type == PACKET_L3_IPV6 && m.ip_version == 6,
          "IPv6 is explicit and unsupported in milestone 1");

    n = make_ipv4(f, sizeof f, 0, 5, PACKET_IPPROTO_UDP, 0, 40000, 1900);
    CHECK(packet_parse_ethernet(f, n, &m) == PACKET_PARSE_OK &&
          m.l3_type == PACKET_L3_IPV4 && m.src_ip4 == 0x0A000002u &&
          m.dst_ip4 == 0x0A000001u && m.src_port == 40000 && m.dst_port == 1900 &&
          m.protocol == PACKET_IPPROTO_UDP && m.l4_present && !m.malformed,
          "plain IPv4 UDP metadata is normalized");

    for (uint32_t len = T_ETH; len < n; len++) {
        CHECK(packet_parse_ethernet(f, len, &m) == PACKET_PARSE_MALFORMED,
              "every truncation of a minimal IPv4/UDP packet is malformed");
    }

    n = make_ipv4(f, sizeof f, 1, 5, PACKET_IPPROTO_UDP, 0, 1234, 53);
    CHECK(packet_parse_ethernet(f, n, &m) == PACKET_PARSE_OK && m.vlan_depth == 1 &&
          m.outer_vlan_id == 100 && m.inner_vlan_id == PACKET_VLAN_NONE && m.dst_port == 53,
          "single VLAN tag is parsed");
    CHECK(packet_parse_ethernet(f, 17, &m) == PACKET_PARSE_MALFORMED,
          "truncated VLAN header is malformed");

    n = make_ipv4(f, sizeof f, 2, 5, PACKET_IPPROTO_UDP, 0, 1234, 53);
    CHECK(packet_parse_ethernet(f, n, &m) == PACKET_PARSE_OK && m.vlan_depth == 2 &&
          m.outer_vlan_id == 100 && m.inner_vlan_id == 200 && m.dst_port == 53,
          "QinQ plus inner VLAN is parsed with a strict depth bound");

    /* Turn the inner payload EtherType into a third tag. */
    put16(f + 20, PACKET_ETHERTYPE_VLAN);
    CHECK(packet_parse_ethernet(f, n, &m) == PACKET_PARSE_UNSUPPORTED && m.vlan_depth == 2,
          "a third VLAN tag stops at the configured bound");

    n = make_ipv4(f, sizeof f, 0, 6, PACKET_IPPROTO_UDP, 0, 1111, 2222);
    CHECK(packet_parse_ethernet(f, n, &m) == PACKET_PARSE_OK &&
          m.ip_header_len == 24 && m.dst_port == 2222,
          "IPv4 options shift the UDP header safely");

    n = make_ipv4(f, sizeof f, 0, 5, PACKET_IPPROTO_UDP, 0, 1, 2);
    f[T_ETH] = 0x45; f[T_ETH] = (uint8_t)((6u << 4) | 5u);
    CHECK(packet_parse_ethernet(f, n, &m) == PACKET_PARSE_MALFORMED,
          "wrong IP version is malformed");
    f[T_ETH] = 0x44;
    CHECK(packet_parse_ethernet(f, n, &m) == PACKET_PARSE_MALFORMED,
          "IPv4 IHL below five is malformed");

    f[T_ETH] = 0x45;
    put16(f + T_ETH + 2, 19);
    CHECK(packet_parse_ethernet(f, n, &m) == PACKET_PARSE_MALFORMED,
          "IP total length below header length is malformed");
    put16(f + T_ETH + 2, 200);
    CHECK(packet_parse_ethernet(f, n, &m) == PACKET_PARSE_MALFORMED,
          "IP total length beyond frame is malformed");

    n = make_ipv4(f, sizeof f, 0, 5, PACKET_IPPROTO_UDP, 0, 1, 2);
    put16(f + T_ETH + T_IP + 4, 7);
    CHECK(packet_parse_ethernet(f, n, &m) == PACKET_PARSE_MALFORMED,
          "UDP length below header size is malformed");
    put16(f + T_ETH + T_IP + 4, 64);
    CHECK(packet_parse_ethernet(f, n, &m) == PACKET_PARSE_MALFORMED,
          "unfragmented UDP length beyond IP payload is malformed");

    n = make_ipv4(f, sizeof f, 0, 5, PACKET_IPPROTO_UDP, 0x2000, 10, 20);
    put16(f + T_ETH + T_IP + 4, 128);
    CHECK(packet_parse_ethernet(f, n, &m) == PACKET_PARSE_OK &&
          m.fragment_state == PACKET_FRAGMENT_FIRST && m.l4_present && m.dst_port == 20,
          "first fragment exposes its L4 header without requiring the complete datagram");

    n = make_ipv4(f, sizeof f, 0, 5, PACKET_IPPROTO_UDP, 0x0001, 10, 20);
    CHECK(packet_parse_ethernet(f, n, &m) == PACKET_PARSE_OK &&
          m.fragment_state == PACKET_FRAGMENT_NONFIRST && !m.l4_present && m.dst_port == 0,
          "non-first fragment is not misparsed as a transport header");

    n = make_ipv4(f, sizeof f, 0, 5, PACKET_IPPROTO_TCP, 0, 50000, 443);
    CHECK(packet_parse_ethernet(f, n, &m) == PACKET_PARSE_OK && m.l4_present &&
          m.src_port == 50000 && m.dst_port == 443 && m.tcp_flags == 0x02,
          "TCP ports and flags are normalized");
    f[T_ETH + T_IP + 12] = 0x40;
    CHECK(packet_parse_ethernet(f, n, &m) == PACKET_PARSE_MALFORMED,
          "TCP data offset below five is malformed");
    f[T_ETH + T_IP + 12] = 0xF0;
    CHECK(packet_parse_ethernet(f, n, &m) == PACKET_PARSE_MALFORMED,
          "TCP header beyond IP payload is malformed");

    n = make_ipv4(f, sizeof f, 0, 5, PACKET_IPPROTO_ICMP, 0, 0, 0);
    CHECK(packet_parse_ethernet(f, n, &m) == PACKET_PARSE_OK &&
          m.protocol == PACKET_IPPROTO_ICMP && m.l4_present,
          "ICMP minimum header is accepted");

    n = make_ipv4(f, sizeof f, 0, 5, 99, 0, 0, 0);
    CHECK(packet_parse_ethernet(f, n, &m) == PACKET_PARSE_OK &&
          m.protocol == 99 && !m.l4_present,
          "unknown IPv4 protocol remains explicit for policy");
}
