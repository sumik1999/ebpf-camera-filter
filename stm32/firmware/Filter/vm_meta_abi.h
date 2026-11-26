#ifndef VM_META_ABI_H
#define VM_META_ABI_H

#include <stdint.h>
#include "packet_meta.h"
#include "packet_parser.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VM_META_ABI_VERSION 1u
#define VM_META_SIZE        32u

enum {
    VM_META_OFF_ABI_VERSION = 0,
    VM_META_OFF_PARSE_STATUS = 1,
    VM_META_OFF_L3_TYPE = 2,
    VM_META_OFF_IP_VERSION = 3,
    VM_META_OFF_PROTOCOL = 4,
    VM_META_OFF_TCP_FLAGS = 5,
    VM_META_OFF_FRAGMENT = 6,
    VM_META_OFF_VLAN_DEPTH = 7,
    VM_META_OFF_L4_PRESENT = 8,
    VM_META_OFF_MULTICAST = 9,
    VM_META_OFF_BROADCAST = 10,
    VM_META_OFF_RESERVED = 11,
    VM_META_OFF_VLAN_ID = 12,
    VM_META_OFF_SRC_PORT = 14,
    VM_META_OFF_DST_PORT = 16,
    VM_META_OFF_IP_TOTAL_LEN = 18,
    VM_META_OFF_SRC_IP4 = 20,
    VM_META_OFF_DST_IP4 = 24,
    VM_META_OFF_FRAME_LEN = 28,
};

/* Encode a canonical, padding-free context. Multi-byte fields are big-endian
 * because VM LDPKT_H/W loads use network byte order. */
void vm_meta_encode(const packet_meta_t *meta, packet_parse_result_t status,
                    uint8_t out[VM_META_SIZE]);

#ifdef __cplusplus
}
#endif

#endif /* VM_META_ABI_H */
