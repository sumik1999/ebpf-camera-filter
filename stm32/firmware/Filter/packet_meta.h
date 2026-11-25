#ifndef PACKET_META_H
#define PACKET_META_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PACKET_VLAN_NONE 0xFFFFu

/* Layer-3 types understood by the normalized parser. */
typedef enum {
    PACKET_L3_OTHER = 0,
    PACKET_L3_ARP,
    PACKET_L3_IPV4,
    PACKET_L3_IPV6,
} packet_l3_t;

/* Fragment position matters because non-first fragments have no L4 header. */
typedef enum {
    PACKET_FRAGMENT_NONE = 0,
    PACKET_FRAGMENT_FIRST,
    PACKET_FRAGMENT_NONFIRST,
} packet_fragment_t;

/* A fixed, naturally aligned summary shared by native policy and a future
 * VM_CTX_META classifier. All multi-byte values are in host byte order. */
typedef struct {
    uint32_t src_ip4;
    uint32_t dst_ip4;
    uint32_t frame_len;
    uint16_t ether_type;
    uint16_t outer_vlan_id;
    uint16_t inner_vlan_id;
    uint16_t ip_total_len;
    uint16_t src_port;
    uint16_t dst_port;
    uint8_t  vlan_depth;
    uint8_t  l3_type;
    uint8_t  ip_version;
    uint8_t  ip_header_len;
    uint8_t  protocol;
    uint8_t  tcp_flags;
    uint8_t  fragment_state;
    uint8_t  multicast;
    uint8_t  broadcast;
    uint8_t  l4_present;
    uint8_t  malformed;
    uint8_t  reserved;
} packet_meta_t;

#ifdef __cplusplus
}
#endif

#endif /* PACKET_META_H */
