/* common.h – shared eBPF helpers for Airport smart-camera defense
 *
 * Mac / Lima adaptation: primary hook is XDP (xdp_md) instead of TC
 * (sk_buff). The packet parser now works on the raw XDP frame directly.
 *
 * Berkeley Packet Filter (BPF) heritage
 * ─────────────────────────────────────
 * The Linux kernel's eBPF derives directly from BSD's BPF (Berkeley, 1993).
 * Key lineage:
 *   • BSD BPF (McCanne & Jacobson 1993) – classic read-only filter VM.
 *   • Linux cBPF – 32-bit port used by tcpdump / socket filters.
 *   • Linux eBPF (kernel 3.18+) – extended 64-bit ISA, maps, helpers,
 *     JIT compiler, and multiple hook points (XDP, TC, kprobe, …).
 *
 * XDP (eXpress Data Path) attaches to the NIC driver's receive hook,
 * before sk_buff allocation.  Return values:
 *   XDP_PASS   – hand packet to normal stack.
 *   XDP_DROP   – drop at driver level (zero-copy discard).
 *   XDP_TX     – reflect packet back out the same interface.
 *
 * Packet parsing (Berkeley-style)
 * ──────────────────────────────
 * Like classic BPF, we walk the wire format layer by layer:
 *   Ethernet header → IP header (IHL check) → L4 header (UDP / TCP).
 * Every pointer is bounds-checked against ctx->data_end to satisfy
 * the eBPF verifier (a strict successor to the original BPF safety
 * guarantees in BSD).
 */

#ifndef AIRPORT_EBPF_COMMON_H
#define AIRPORT_EBPF_COMMON_H

#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>
#include "maps.h"

/* ── Constants ────────────────────────────────────────────────────────── */
#define APOC_IP_BE  bpf_htonl(0x0a0a0002u)   /* 10.10.0.2 in network byte order */
#define ONE_SEC_NS  1000000000ULL

/* ── Flow key ─────────────────────────────────────────────────────────── */
/* Mirrors maps.h struct flow_key; src/dst stored in host byte order
 * after extraction so the hash function stays platform-independent. */

/* ── Inline helpers ───────────────────────────────────────────────────── */

/* Jenkin-style finaliser – fast, low-collision for 5-tuple keys.        */
static __always_inline __u32 flow_hash(const struct flow_key *k)
{
    __u32 h = k->src ^ k->dst ^ ((__u32)k->dport << 16) ^ k->proto;
    h ^= h >> 16;
    h *= 0x7feb352dU;
    h ^= h >> 15;
    h *= 0x846ca68bU;
    h ^= h >> 16;
    return h;
}

/*
 * parse_packet – Berkeley-style layered packet walk for XDP frames.
 *
 * Parameters
 *   ctx    : XDP context carrying data / data_end pointers.
 *   key    : output flow key (filled on success).
 *   is_syn : output flag, set to 1 for TCP SYN (no ACK) packets.
 *
 * Returns 1 on success, 0 if the packet should be passed untouched
 * (non-IP, malformed, or unsupported L4 protocol).
 *
 * Layer-by-layer walk (BPF heritage)
 * ───────────────────────────────────
 *  1. Ethernet header  – verify ETH_P_IP ethertype.
 *  2. IPv4 header      – verify IHL ≥ 5, extract src/dst/proto.
 *  3. L4 header        – extract destination port; detect TCP SYN.
 *
 * Every bounds check mirrors what classic BPF's packet accessors
 * enforced at the interpreter level; here the eBPF verifier enforces
 * them statically at load time.
 */
static __always_inline int parse_packet(struct xdp_md *ctx,
                                        struct flow_key *key,
                                        int *is_syn)
{
    void *data     = (void *)(long)ctx->data;
    void *data_end = (void *)(long)ctx->data_end;

    /* ── Layer 2: Ethernet ─────────────────────────────────────────── */
    struct ethhdr *eth = data;
    if ((void *)(eth + 1) > data_end)
        return 0;
    if (eth->h_proto != bpf_htons(ETH_P_IP))
        return 0;                    /* drop non-IP silently (pass) */

    /* ── Layer 3: IPv4 ─────────────────────────────────────────────── */
    struct iphdr *ip = (void *)(eth + 1);
    if ((void *)(ip + 1) > data_end)
        return 0;
    if (ip->ihl < 5)                 /* malformed – pass to stack */
        return 0;

    /* Advance past variable-length IP options (IHL * 4 bytes).        */
    void *l4 = (void *)ip + ((int)ip->ihl * 4);
    if (l4 > data_end)
        return 0;

    /* Fill flow key (host byte order for hash consistency).            */
    key->src   = bpf_ntohl(ip->saddr);
    key->dst   = bpf_ntohl(ip->daddr);
    key->proto = ip->protocol;
    key->pad   = 0;
    key->dport = 0;
    *is_syn    = 0;

    /* ── Layer 4: UDP / TCP ─────────────────────────────────────────── */
    if (ip->protocol == IPPROTO_UDP) {
        struct udphdr *udp = l4;
        if ((void *)(udp + 1) > data_end)
            return 0;
        key->dport = bpf_ntohs(udp->dest);
        return 1;
    }

    if (ip->protocol == IPPROTO_TCP) {
        struct tcphdr *tcp = l4;
        if ((void *)(tcp + 1) > data_end)
            return 0;
        key->dport = bpf_ntohs(tcp->dest);
        /*
         * SYN detection: classic BPF programs used bit-masking on the
         * raw flags byte; eBPF structs expose named bitfields directly.
         */
        *is_syn = (tcp->syn && !tcp->ack) ? 1 : 0;
        return 1;
    }

    return 0;   /* unsupported L4: pass to stack */
}

/*
 * whitelisted – exempt APOC telemetry traffic from rate-limiting.
 *
 * Matches source OR destination == APOC server IP so that both
 * outbound probes and inbound replies are whitelisted.
 */
static __always_inline int whitelisted(const struct flow_key *key)
{
    __u32 apoc_host = bpf_ntohl(APOC_IP_BE);
    return (key->src == apoc_host || key->dst == apoc_host);
}

#endif /* AIRPORT_EBPF_COMMON_H */
