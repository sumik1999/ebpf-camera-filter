/* Shared packet parsing helpers for TC ingress classifiers. */
#ifndef AIRPORT_EBPF_TC_COMMON_H
#define AIRPORT_EBPF_TC_COMMON_H

#include <linux/pkt_cls.h>
#include "common.h"

/* TC wrapper around the same parser used by XDP. */
static __always_inline int parse_packet_tc(struct __sk_buff *skb,
                                           struct flow_key *key,
                                           int *is_syn)
{
    void *data = (void *)(long)skb->data;
    void *data_end = (void *)(long)skb->data_end;

    return parse_frame(data, data_end, key, is_syn) == AIRPORT_PARSE_OK;
}

#endif
