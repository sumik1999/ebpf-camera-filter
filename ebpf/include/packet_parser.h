/* Verifier-safe Ethernet/VLAN/IPv4/TCP/UDP parser shared by XDP and TC. */
#ifndef AIRPORT_EBPF_PACKET_PARSER_H
#define AIRPORT_EBPF_PACKET_PARSER_H

#define AIRPORT_PARSE_MALFORMED (-1)
#define AIRPORT_PARSE_SKIP       0
#define AIRPORT_PARSE_OK         1
#define AIRPORT_PARSE_IPV4       2
#define AIRPORT_MAX_VLANS        2
#define AIRPORT_IP_OFFSET_MASK   0x1fff
#define AIRPORT_IP_MORE_FRAGS    0x2000

struct airport_vlan_hdr {
    __be16 tci;
    __be16 encapsulated_proto;
};

/*
 * Return values distinguish malformed traffic from valid traffic that does
 * not need flow accounting. XDP drops MALFORMED; CMS/Elastic stages process
 * only OK and leave SKIP traffic to subsequent policy layers.
 */
static __always_inline int parse_frame(void *data, void *data_end,
                                       struct flow_key *key, int *is_syn)
{
    struct ethhdr *eth = data;
    if ((void *)(eth + 1) > data_end)
        return AIRPORT_PARSE_MALFORMED;

    __be16 protocol = eth->h_proto;
    void *cursor = (void *)(eth + 1);

#pragma unroll
    for (int i = 0; i < AIRPORT_MAX_VLANS; i++) {
        if (protocol != bpf_htons(ETH_P_8021Q) &&
            protocol != bpf_htons(ETH_P_8021AD))
            break;
        struct airport_vlan_hdr *vlan = cursor;
        if ((void *)(vlan + 1) > data_end)
            return AIRPORT_PARSE_MALFORMED;
        protocol = vlan->encapsulated_proto;
        cursor = (void *)(vlan + 1);
    }

    /* More than two tags is outside the supported camera-network policy and
     * could otherwise bypass flow accounting. Treat it as malformed. */
    if (protocol == bpf_htons(ETH_P_8021Q) ||
        protocol == bpf_htons(ETH_P_8021AD))
        return AIRPORT_PARSE_MALFORMED;
    if (protocol != bpf_htons(ETH_P_IP))
        return AIRPORT_PARSE_SKIP;

    struct iphdr *ip = cursor;
    if ((void *)(ip + 1) > data_end)
        return AIRPORT_PARSE_MALFORMED;
    if (ip->version != 4 || ip->ihl < 5)
        return AIRPORT_PARSE_MALFORMED;

    __u32 ihl_len = (__u32)ip->ihl * 4;
    __u32 total_len = bpf_ntohs(ip->tot_len);
    void *l4 = (void *)ip + ihl_len;
    if (total_len < ihl_len || l4 > data_end)
        return AIRPORT_PARSE_MALFORMED;
    if ((__u64)total_len > (__u64)(data_end - (void *)ip))
        return AIRPORT_PARSE_MALFORMED;

    key->src = bpf_ntohl(ip->saddr);
    key->dst = bpf_ntohl(ip->daddr);
    key->sport = 0;
    key->dport = 0;
    key->proto = ip->protocol;
    key->pad[0] = 0;
    key->pad[1] = 0;
    key->pad[2] = 0;
    *is_syn = 0;

    __u16 fragment = bpf_ntohs(ip->frag_off);
    if (fragment & (AIRPORT_IP_OFFSET_MASK | AIRPORT_IP_MORE_FRAGS))
        return AIRPORT_PARSE_IPV4;

    if (ip->protocol == IPPROTO_UDP) {
        struct udphdr *udp = l4;
        if ((void *)(udp + 1) > data_end ||
            total_len - ihl_len < sizeof(*udp))
            return AIRPORT_PARSE_MALFORMED;
        __u32 udp_len = bpf_ntohs(udp->len);
        if (udp_len < sizeof(*udp) || udp_len > total_len - ihl_len)
            return AIRPORT_PARSE_MALFORMED;
        key->sport = bpf_ntohs(udp->source);
        key->dport = bpf_ntohs(udp->dest);
        return AIRPORT_PARSE_OK;
    }

    if (ip->protocol == IPPROTO_TCP) {
        struct tcphdr *tcp = l4;
        if ((void *)(tcp + 1) > data_end ||
            total_len - ihl_len < sizeof(*tcp) || tcp->doff < 5)
            return AIRPORT_PARSE_MALFORMED;
        __u32 tcp_len = (__u32)tcp->doff * 4;
        if (tcp_len > total_len - ihl_len ||
            (void *)tcp + tcp_len > data_end)
            return AIRPORT_PARSE_MALFORMED;
        key->sport = bpf_ntohs(tcp->source);
        key->dport = bpf_ntohs(tcp->dest);
        *is_syn = tcp->syn && !tcp->ack;
        return AIRPORT_PARSE_OK;
    }

    return AIRPORT_PARSE_IPV4;
}

#endif
