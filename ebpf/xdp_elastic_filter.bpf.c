/* xdp_elastic_filter.bpf.c – ElasticSketch-inspired XDP flood filter
 *
 * Mac / Lima adaptation of the original WSL2 TC-ingress elastic filter.
 *
 * Attachment point: XDP (same as xdp_cms_filter.bpf.c).
 *
 * ElasticSketch design (adapted for XDP eBPF)
 * ─────────────────────────────────────────────
 * Classic CMS treats all flows equally, so a heavy attacker flow can
 * "pollute" the counter bucket shared by a low-rate APOC telemetry flow
 * (see xdp_cms_filter.bpf.c for discussion of this weakness).
 *
 * ElasticSketch separates traffic into two stages:
 *
 *   Heavy-flow table (LRU hash map)
 *     Tracks flows whose estimated rate has already exceeded the
 *     HEAVY_FLOW_THRESHOLD_PPS threshold.  Each entry stores a precise
 *     per-flow counter.  Once a flow enters this table it stays there
 *     (until LRU eviction) and its counter is never mixed with other
 *     flows.
 *
 *   Light-flow sketch (Count-Min Sketch, single-row for simplicity)
 *     Absorbs new and low-rate flows.  When a bucket's estimate crosses
 *     the threshold, the flow is promoted into the heavy table so its
 *     counter is isolated from lighter traffic going forward.
 *
 * Trusted traffic policy
 *   No source IP bypasses accounting. Low-rate telemetry remains below the
 *   sketch threshold, and nftables applies a bounded service-specific policy.
 *
 * BPF maps used
 *   heavy  : BPF_MAP_TYPE_LRU_HASH  – per-flow precise counters.
 *   light  : BPF_MAP_TYPE_ARRAY     – CMS single-row sketch.
 *
 * Berkeley Packet Filter (BPF) map heritage
 * ──────────────────────────────────────────
 * Classic BSD BPF had no persistent state between filter calls.  Linux
 * eBPF maps introduced shared, kernel-resident key-value stores that
 * survive across packet invocations and are accessible from user-space
 * via bpf(2) syscall – a major architectural extension beyond the
 * original BPF VM.
 */

#include "common.h"

/* ── Heavy-flow table: LRU hash keyed on 5-tuple ─────────────────────── */
struct {
    __uint(type,        BPF_MAP_TYPE_LRU_HASH);
    __uint(max_entries, HEAVY_TABLE_SIZE);
    __type(key,         struct flow_key);
    __type(value,       struct flow_state);
} heavy SEC(".maps");

/* ── Light-flow sketch: single-row Count-Min Sketch ──────────────────── */
struct {
    __uint(type,        BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, CMS_WIDTH);
    __type(key,         __u32);
    __type(value,       struct bucket);
} light SEC(".maps");

/*
 * xdp_ingress – XDP entry point implementing ElasticSketch logic.
 *
 * Decision flow
 *  1. Parse packet (Berkeley-style layered walk in common.h).
 *  2. Filter only UDP floods and TCP SYN floods; no IP is exempt.
 *  3a. If flow already in heavy table → update precise counter.
 *      If counter > threshold → XDP_DROP.
 *  3b. If flow NOT in heavy table → update light sketch bucket.
 *      If sketch estimate > threshold → promote flow to heavy table
 *      and DROP the triggering packet.
 *  4. Below threshold → XDP_PASS.
 */
SEC("xdp")
int xdp_ingress(struct xdp_md *ctx)
{
    struct flow_key key = {};
    int is_syn = 0;

    /* Step 1: parse Ethernet / IP / L4 */
    if (!parse_packet(ctx, &key, &is_syn))
        return XDP_PASS;

    /* Step 2: process attack-pattern traffic. No source IP bypasses flow
     * accounting; trusted-service policy is enforced later at nftables. */
    if (key.proto != IPPROTO_UDP &&
        !(key.proto == IPPROTO_TCP && is_syn))
        return XDP_PASS;

    __u64 now = bpf_ktime_get_ns();

    /* ── Step 4a: heavy-flow table lookup ──────────────────────────── */
    struct flow_state *state = bpf_map_lookup_elem(&heavy, &key);
    if (state) {
        /* Flow is already classified as heavy. */
        if (now - state->window_ns > ONE_SEC_NS) {
            state->window_ns = now;
            state->count     = 1;
        } else {
            state->count += 1;
        }
        if (state->count > DEFAULT_THRESHOLD_PPS) {
            __sync_fetch_and_add(&state->drops, 1);
            return XDP_DROP;
        }
        return XDP_PASS;
    }

    /* ── Step 4b: light sketch update ──────────────────────────────── */
    /*
     * Berkeley-style hash-and-index: a single hash value selects the
     * counter bucket.  (The heavy table above provides the per-flow
     * isolation that removes CMS collision for promoted flows.)
     */
    __u32 idx = flow_hash(&key) & ((__u32)CMS_WIDTH - 1);
    struct bucket *b = bpf_map_lookup_elem(&light, &idx);
    if (!b)
        return XDP_PASS;

    if (now - b->window_ns > ONE_SEC_NS) {
        b->window_ns = now;
        b->count     = 1;
    } else {
        b->count += 1;
    }

    if (b->count > DEFAULT_THRESHOLD_PPS) {
        /*
         * Promote flow to heavy table to isolate it from lighter flows
         * that happen to hash to the same sketch bucket.
         */
        struct flow_state new_state = {
            .window_ns = now,
            .count     = b->count,
            .drops     = 1,
        };
        bpf_map_update_elem(&heavy, &key, &new_state, BPF_ANY);
        return XDP_DROP;
    }

    return XDP_PASS;
}

char _license[] SEC("license") = "GPL";
