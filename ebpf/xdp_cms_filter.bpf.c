/* xdp_cms_filter.bpf.c – Count-Min Sketch XDP flood filter
 *
 * Mac / Lima adaptation of the original WSL2 TC-ingress CMS filter.
 *
 * Attachment point: XDP (eXpress Data Path) instead of TC ingress.
 * The program runs at the NIC's earliest receive hook, before sk_buff
 * allocation, giving the lowest possible latency for packet drops.
 *
 * On Lima / OrbStack VMs with virtual Ethernet (veth) pairs, XDP runs
 * in SKB/generic mode (--mode skb via ip link set … xdp …).  On bare-
 * metal NICs with driver support it runs in native mode.
 *
 * Count-Min Sketch (CMS) algorithm
 * ──────────────────────────────────
 * A CMS estimates per-flow packet rates using d independent hash rows,
 * each of width w counters.  For each arriving packet:
 *   for row r in [0, d):
 *       idx = hash_r(flow_key) mod w
 *       counter[r][idx] += 1
 *   estimate = min over r of counter[r][hash_r(key)]
 * If estimate > threshold → DROP.
 *
 * Known weakness: hash collisions cause a low-rate telemetry flow to
 * share a counter with a high-rate attack flow → telemetry estimate is
 * inflated.  The ElasticSketch filter (xdp_elastic_filter.bpf.c) fixes
 * this by separating heavy flows into a dedicated hash table.
 *
 * BPF map: single ARRAY with (CMS_DEPTH × CMS_WIDTH) entries.
 *          Row r starts at index r * CMS_WIDTH.
 */

#include "common.h"

/* ── CMS BPF array map ─────────────────────────────────────────────────
 * Layout: row 0 → indices [0, CMS_WIDTH)
 *         row 1 → indices [CMS_WIDTH, 2*CMS_WIDTH)  …
 */
struct {
    __uint(type,       BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, CMS_BUCKETS);          /* CMS_DEPTH * CMS_WIDTH  */
    __type(key,        __u32);
    __type(value,      struct bucket);
} cms SEC(".maps");

/*
 * xdp_ingress – XDP entry point (replaces tc_ingress from WSL2 build).
 *
 * Steps
 *  1. Parse Ethernet/IP/L4 headers (Berkeley-style layered walk).
 *  2. Process only UDP floods and TCP SYN floods; no IP is exempt.
 *  3. Update all CMS rows and compute minimum count estimate.
 *  4. If estimate > threshold → XDP_DROP; else → XDP_PASS.
 */
SEC("xdp")
int xdp_ingress(struct xdp_md *ctx)
{
    struct flow_key key = {};
    int is_syn = 0;

    /* Step 1: parse packet headers */
    if (!parse_packet(ctx, &key, &is_syn))
        return XDP_PASS;

    /* Step 2: only rate-limit UDP floods and TCP SYN floods. Trusted
     * services remain rate-limited; policy is applied later at nftables. */
    if (key.proto != IPPROTO_UDP &&
        !(key.proto == IPPROTO_TCP && is_syn))
        return XDP_PASS;

    /* Step 4: CMS update – walk all depth rows */
    __u64  now       = bpf_ktime_get_ns();
    __u32  h         = flow_hash(&key);
    __u32  min_count = 0xffffffffU;

#pragma unroll
    for (int depth = 0; depth < CMS_DEPTH; depth++) {
        /*
         * Derive a distinct hash per row using a Knuth multiplicative
         * mix step.  This mimics the multiple independent hash functions
         * required by the CMS algorithm without needing d separate hash
         * implementations.
         */
        __u32 idx = (h + (__u32)depth * 0x9e3779b9U) & (CMS_WIDTH - 1);
        idx += (__u32)depth * CMS_WIDTH;   /* offset into the correct row */

        struct bucket *b = bpf_map_lookup_elem(&cms, &idx);
        if (!b)
            continue;

        /* Sliding 1-second window: reset counter if window has expired. */
        if (now - b->window_ns > ONE_SEC_NS) {
            b->window_ns = now;
            b->count     = 1;
        } else {
            b->count += 1;
        }

        if (b->count < min_count)
            min_count = b->count;
    }

    /* Step 5: rate decision */
    if (min_count > DEFAULT_THRESHOLD_PPS)
        return XDP_DROP;

    return XDP_PASS;
}

char _license[] SEC("license") = "GPL";
