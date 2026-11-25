#ifndef PACKET_PARSER_H
#define PACKET_PARSER_H

#include <stdint.h>
#include "packet_meta.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PACKET_ETHERTYPE_IPV4  0x0800u
#define PACKET_ETHERTYPE_ARP   0x0806u
#define PACKET_ETHERTYPE_VLAN  0x8100u
#define PACKET_ETHERTYPE_QINQ  0x88A8u
#define PACKET_ETHERTYPE_IPV6  0x86DDu

#define PACKET_IPPROTO_ICMP    1u
#define PACKET_IPPROTO_TCP     6u
#define PACKET_IPPROTO_UDP     17u

/* PARSE_OK includes recognized and unrecognized EtherTypes/protocols. The
 * metadata says what was recognized. PARSE_UNSUPPORTED is reserved for a
 * structurally valid frame whose bounded parser deliberately stops (currently
 * IPv6 or more than two VLAN tags). PARSE_MALFORMED means the frame is unsafe
 * or internally inconsistent and should normally be dropped. */
typedef enum {
    PACKET_PARSE_OK = 0,
    PACKET_PARSE_UNSUPPORTED,
    PACKET_PARSE_MALFORMED,
} packet_parse_result_t;

packet_parse_result_t packet_parse_ethernet(const uint8_t *frame, uint32_t len,
                                            packet_meta_t *meta);

#ifdef __cplusplus
}
#endif

#endif /* PACKET_PARSER_H */
