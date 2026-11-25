#include "packet_parser.h"

#include <stddef.h>
#include <string.h>

#define ETH_HEADER_LEN 14u
#define VLAN_HEADER_LEN 4u
#define IPV4_MIN_HEADER_LEN 20u
#define UDP_HEADER_LEN 8u
#define TCP_MIN_HEADER_LEN 20u
#define ICMP_MIN_HEADER_LEN 4u

static uint16_t read_be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static uint32_t read_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static int bytes_available(uint32_t len, uint32_t offset, uint32_t needed)
{
    return offset <= len && needed <= len - offset;
}

static packet_parse_result_t malformed(packet_meta_t *meta)
{
    meta->malformed = 1u;
    return PACKET_PARSE_MALFORMED;
}

packet_parse_result_t packet_parse_ethernet(const uint8_t *frame, uint32_t len,
                                            packet_meta_t *meta)
{
    uint32_t l3_offset = ETH_HEADER_LEN;
    uint16_t ether_type;

    if (meta == NULL) {
        return PACKET_PARSE_MALFORMED;
    }
    memset(meta, 0, sizeof(*meta));
    meta->frame_len = len;
    meta->outer_vlan_id = PACKET_VLAN_NONE;
    meta->inner_vlan_id = PACKET_VLAN_NONE;

    if (frame == NULL || !bytes_available(len, 0u, ETH_HEADER_LEN)) {
        return malformed(meta);
    }

    meta->multicast = (uint8_t)((frame[0] & 1u) != 0u);
    meta->broadcast = (uint8_t)(frame[0] == 0xFFu && frame[1] == 0xFFu &&
                                frame[2] == 0xFFu && frame[3] == 0xFFu &&
                                frame[4] == 0xFFu && frame[5] == 0xFFu);
    ether_type = read_be16(frame + 12u);

    while (ether_type == PACKET_ETHERTYPE_VLAN || ether_type == PACKET_ETHERTYPE_QINQ) {
        uint16_t vlan_id;
        if (meta->vlan_depth >= 2u) {
            meta->ether_type = ether_type;
            return PACKET_PARSE_UNSUPPORTED;
        }
        if (!bytes_available(len, l3_offset, VLAN_HEADER_LEN)) {
            return malformed(meta);
        }
        vlan_id = (uint16_t)(read_be16(frame + l3_offset) & 0x0FFFu);
        if (meta->vlan_depth == 0u) {
            meta->outer_vlan_id = vlan_id;
        } else {
            meta->inner_vlan_id = vlan_id;
        }
        meta->vlan_depth++;
        ether_type = read_be16(frame + l3_offset + 2u);
        l3_offset += VLAN_HEADER_LEN;
    }

    meta->ether_type = ether_type;
    if (ether_type == PACKET_ETHERTYPE_ARP) {
        meta->l3_type = PACKET_L3_ARP;
        return PACKET_PARSE_OK;
    }
    if (ether_type == PACKET_ETHERTYPE_IPV6) {
        meta->l3_type = PACKET_L3_IPV6;
        meta->ip_version = 6u;
        return PACKET_PARSE_UNSUPPORTED;
    }
    if (ether_type != PACKET_ETHERTYPE_IPV4) {
        meta->l3_type = PACKET_L3_OTHER;
        return PACKET_PARSE_OK;
    }

    meta->l3_type = PACKET_L3_IPV4;
    if (!bytes_available(len, l3_offset, IPV4_MIN_HEADER_LEN)) {
        return malformed(meta);
    }

    const uint8_t *ip = frame + l3_offset;
    uint8_t version = (uint8_t)(ip[0] >> 4);
    uint8_t ihl_words = (uint8_t)(ip[0] & 0x0Fu);
    uint32_t ip_header_len = (uint32_t)ihl_words * 4u;
    uint16_t ip_total_len = read_be16(ip + 2u);

    meta->ip_version = version;
    meta->ip_header_len = (uint8_t)ip_header_len;
    meta->ip_total_len = ip_total_len;
    if (version != 4u || ihl_words < 5u ||
        !bytes_available(len, l3_offset, ip_header_len) ||
        ip_total_len < ip_header_len ||
        !bytes_available(len, l3_offset, ip_total_len)) {
        return malformed(meta);
    }

    meta->protocol = ip[9];
    meta->src_ip4 = read_be32(ip + 12u);
    meta->dst_ip4 = read_be32(ip + 16u);

    uint16_t fragment = read_be16(ip + 6u);
    uint16_t fragment_offset = (uint16_t)(fragment & 0x1FFFu);
    uint8_t more_fragments = (uint8_t)((fragment & 0x2000u) != 0u);
    if (fragment_offset != 0u) {
        meta->fragment_state = PACKET_FRAGMENT_NONFIRST;
        return PACKET_PARSE_OK;
    }
    if (more_fragments) {
        meta->fragment_state = PACKET_FRAGMENT_FIRST;
    } else {
        meta->fragment_state = PACKET_FRAGMENT_NONE;
    }

    uint32_t l4_offset = l3_offset + ip_header_len;
    uint32_t ip_payload_len = (uint32_t)ip_total_len - ip_header_len;

    if (meta->protocol == PACKET_IPPROTO_UDP) {
        if (ip_payload_len < UDP_HEADER_LEN || !bytes_available(len, l4_offset, UDP_HEADER_LEN)) {
            return malformed(meta);
        }
        const uint8_t *udp = frame + l4_offset;
        uint16_t udp_len = read_be16(udp + 4u);
        if (udp_len < UDP_HEADER_LEN ||
            (meta->fragment_state == PACKET_FRAGMENT_NONE && udp_len > ip_payload_len)) {
            return malformed(meta);
        }
        meta->src_port = read_be16(udp);
        meta->dst_port = read_be16(udp + 2u);
        meta->l4_present = 1u;
        return PACKET_PARSE_OK;
    }

    if (meta->protocol == PACKET_IPPROTO_TCP) {
        if (ip_payload_len < TCP_MIN_HEADER_LEN ||
            !bytes_available(len, l4_offset, TCP_MIN_HEADER_LEN)) {
            return malformed(meta);
        }
        const uint8_t *tcp = frame + l4_offset;
        uint32_t tcp_header_len = (uint32_t)(tcp[12] >> 4) * 4u;
        if (tcp_header_len < TCP_MIN_HEADER_LEN || tcp_header_len > ip_payload_len ||
            !bytes_available(len, l4_offset, tcp_header_len)) {
            return malformed(meta);
        }
        meta->src_port = read_be16(tcp);
        meta->dst_port = read_be16(tcp + 2u);
        meta->tcp_flags = tcp[13];
        meta->l4_present = 1u;
        return PACKET_PARSE_OK;
    }

    if (meta->protocol == PACKET_IPPROTO_ICMP) {
        if (ip_payload_len < ICMP_MIN_HEADER_LEN ||
            !bytes_available(len, l4_offset, ICMP_MIN_HEADER_LEN)) {
            return malformed(meta);
        }
        meta->l4_present = 1u;
    }

    return PACKET_PARSE_OK;
}
