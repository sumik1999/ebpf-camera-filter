#include "vm_meta_abi.h"

#include <stddef.h>

static void put_be16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)(value >> 8);
    p[1] = (uint8_t)value;
}

static void put_be32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

void vm_meta_encode(const packet_meta_t *meta, packet_parse_result_t status,
                    uint8_t out[VM_META_SIZE])
{
    if (out == NULL) {
        return;
    }
    for (uint32_t i = 0; i < VM_META_SIZE; i++) {
        out[i] = 0u;
    }
    out[VM_META_OFF_ABI_VERSION] = VM_META_ABI_VERSION;
    out[VM_META_OFF_PARSE_STATUS] = (uint8_t)status;
    if (meta == NULL) {
        out[VM_META_OFF_PARSE_STATUS] = (uint8_t)PACKET_PARSE_MALFORMED;
        return;
    }

    out[VM_META_OFF_L3_TYPE] = meta->l3_type;
    out[VM_META_OFF_IP_VERSION] = meta->ip_version;
    out[VM_META_OFF_PROTOCOL] = meta->protocol;
    out[VM_META_OFF_TCP_FLAGS] = meta->tcp_flags;
    out[VM_META_OFF_FRAGMENT] = meta->fragment_state;
    out[VM_META_OFF_VLAN_DEPTH] = meta->vlan_depth;
    out[VM_META_OFF_L4_PRESENT] = meta->l4_present;
    out[VM_META_OFF_MULTICAST] = meta->multicast;
    out[VM_META_OFF_BROADCAST] = meta->broadcast;

    uint16_t vlan = PACKET_VLAN_NONE;
    if (meta->vlan_depth == 1u) {
        vlan = meta->outer_vlan_id;
    } else if (meta->vlan_depth >= 2u) {
        vlan = meta->inner_vlan_id;
    }
    put_be16(out + VM_META_OFF_VLAN_ID, vlan);
    put_be16(out + VM_META_OFF_SRC_PORT, meta->src_port);
    put_be16(out + VM_META_OFF_DST_PORT, meta->dst_port);
    put_be16(out + VM_META_OFF_IP_TOTAL_LEN, meta->ip_total_len);
    put_be32(out + VM_META_OFF_SRC_IP4, meta->src_ip4);
    put_be32(out + VM_META_OFF_DST_IP4, meta->dst_ip4);
    put_be32(out + VM_META_OFF_FRAME_LEN, meta->frame_len);
}
