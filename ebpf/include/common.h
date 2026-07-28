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
 *   Ethernet → up to two VLAN tags → IPv4 → UDP/TCP five-tuple.
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
#include "packet_parser.h"

/* ── Constants ────────────────────────────────────────────────────────── */
#define ONE_SEC_NS  1000000000ULL

/* ── Flow key ─────────────────────────────────────────────────────────── */
/* Mirrors maps.h struct flow_key; src/dst stored in host byte order
 * after extraction so the hash function stays platform-independent. */

/* ── Inline helpers ───────────────────────────────────────────────────── */

/* Jenkin-style finaliser – fast, low-collision for 5-tuple keys.        */
static __always_inline __u32 flow_hash(const struct flow_key *k)
{
    __u32 ports = ((__u32)k->sport << 16) | k->dport;
    __u32 h = k->src ^ (k->dst * 0x9e3779b9U) ^ ports ^ k->proto;
    h ^= h >> 16;
    h *= 0x7feb352dU;
    h ^= h >> 15;
    h *= 0x846ca68bU;
    h ^= h >> 16;
    return h;
}

/* XDP wrapper around the shared wire-format parser. */
static __always_inline int parse_packet(struct xdp_md *ctx,
                                        struct flow_key *key,
                                        int *is_syn)
{
    void *data = (void *)(long)ctx->data;
    void *data_end = (void *)(long)ctx->data_end;

    return parse_frame(data, data_end, key, is_syn) == AIRPORT_PARSE_OK;
}

#endif /* AIRPORT_EBPF_COMMON_H */
