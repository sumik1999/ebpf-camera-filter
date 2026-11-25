/* RAPID T-or-N dataplane guard.
 *
 * Attach rapid_ingress and rapid_egress to every Linux bridge port.  Both
 * programs share the pinned port_stats and blocked_ports maps.  Ingress
 * evaluates the thesis' signed Delta2 metric whenever either T expires or N
 * packets have been observed.  A confirmed edge anomaly is inserted into
 * blocked_ports and the triggering packet is dropped immediately in TC.
 */
#include <linux/bpf.h>
#include <linux/pkt_cls.h>
#include <bpf/bpf_helpers.h>

#ifndef RAPID_WINDOW_NS
#define RAPID_WINDOW_NS 500000000ULL
#endif
#ifndef RAPID_PACKET_TRIGGER
#define RAPID_PACKET_TRIGGER 1000ULL
#endif
#ifndef RAPID_MIN_PACKETS
#define RAPID_MIN_PACKETS 50ULL
#endif
#ifndef RAPID_DELTA2_MILLI
#define RAPID_DELTA2_MILLI 500
#endif

struct rapid_port_state {
    __u64 window_start_ns;
    __u64 rx_packets;
    __u64 tx_packets;
    __u64 window_rx;
    __u64 window_tx;
    __u64 dropped_packets;
    __u64 trigger_count;
    __u64 last_trigger_ns;
    __s32 delta2_milli;
    __u32 blocked;
};

struct rapid_event {
    __u64 timestamp_ns;
    __u32 ifindex;
    __u32 action;
    __u64 window_rx;
    __u64 window_tx;
    __s32 delta2_milli;
    __u32 reason; /* 1=N packets, 2=T expired */
};

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 64);
    __type(key, __u32);
    __type(value, struct rapid_port_state);
} port_stats SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 64);
    __type(key, __u32);
    __type(value, __u8);
} blocked_ports SEC(".maps");

/* Only controller-designated, untrusted edge ports may self-block. */
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 64);
    __type(key, __u32);
    __type(value, __u8);
} guarded_ports SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 256 * 1024);
} rapid_events SEC(".maps");

static __always_inline struct rapid_port_state *get_state(__u32 ifindex, __u64 now)
{
    struct rapid_port_state *state = bpf_map_lookup_elem(&port_stats, &ifindex);
    if (state)
        return state;

    struct rapid_port_state initial = { .window_start_ns = now };
    bpf_map_update_elem(&port_stats, &ifindex, &initial, BPF_NOEXIST);
    return bpf_map_lookup_elem(&port_stats, &ifindex);
}

static __always_inline void emit_event(__u64 now, __u32 ifindex,
                                       struct rapid_port_state *state, __u32 reason)
{
    struct rapid_event *event = bpf_ringbuf_reserve(&rapid_events, sizeof(*event), 0);
    if (!event)
        return;
    event->timestamp_ns = now;
    event->ifindex = ifindex;
    event->action = 1;
    event->window_rx = state->window_rx;
    event->window_tx = state->window_tx;
    event->delta2_milli = state->delta2_milli;
    event->reason = reason;
    bpf_ringbuf_submit(event, 0);
}

SEC("tc/ingress")
int rapid_ingress(struct __sk_buff *skb)
{
    __u32 ifindex = skb->ifindex;
    __u8 *blocked = bpf_map_lookup_elem(&blocked_ports, &ifindex);
    __u64 now = bpf_ktime_get_ns();
    struct rapid_port_state *state = get_state(ifindex, now);

    if (blocked && *blocked) {
        if (state)
            __sync_fetch_and_add(&state->dropped_packets, 1);
        return TC_ACT_SHOT;
    }
    if (!state)
        return TC_ACT_OK;

    __sync_fetch_and_add(&state->rx_packets, 1);
    __sync_fetch_and_add(&state->window_rx, 1);

    /* Transit and trusted edge ports contribute counters but can only be
     * mitigated by the topology controller, never by this fast path. */
    __u8 *guarded = bpf_map_lookup_elem(&guarded_ports, &ifindex);
    if (!guarded || !*guarded)
        return TC_ACT_OK;

    __u64 rx = state->window_rx;
    __u64 tx = state->window_tx;
    __u64 total = rx + tx;
    __u64 elapsed = now - state->window_start_ns;
    __u32 reason = total >= RAPID_PACKET_TRIGGER ? 1 :
                   (elapsed >= RAPID_WINDOW_NS ? 2 : 0);
    if (!reason)
        return TC_ACT_OK;

    state->trigger_count += 1;
    state->last_trigger_ns = now;
    state->delta2_milli = 0;
    if (total >= RAPID_MIN_PACKETS && rx > tx) {
        __u64 difference = rx - tx;
        if (difference * 1000ULL > total * RAPID_DELTA2_MILLI) {
            __u8 one = 1;
            state->delta2_milli = (__s32)((difference * 1000ULL) / total);
            state->blocked = 1;
            bpf_map_update_elem(&blocked_ports, &ifindex, &one, BPF_ANY);
            emit_event(now, ifindex, state, reason);
            state->window_start_ns = now;
            state->window_rx = 0;
            state->window_tx = 0;
            __sync_fetch_and_add(&state->dropped_packets, 1);
            return TC_ACT_SHOT;
        }
        state->delta2_milli = (__s32)((difference * 1000ULL) / total);
    } else if (total && tx > rx) {
        state->delta2_milli = -(__s32)(((tx - rx) * 1000ULL) / total);
    }

    state->window_start_ns = now;
    state->window_rx = 0;
    state->window_tx = 0;
    return TC_ACT_OK;
}

SEC("tc/egress")
int rapid_egress(struct __sk_buff *skb)
{
    __u32 ifindex = skb->ifindex;
    __u64 now = bpf_ktime_get_ns();
    struct rapid_port_state *state = get_state(ifindex, now);
    if (state) {
        __sync_fetch_and_add(&state->tx_packets, 1);
        __sync_fetch_and_add(&state->window_tx, 1);
    }
    return TC_ACT_OK;
}

char _license[] SEC("license") = "GPL";
