/* ElasticSketch-inspired flood filter for a TC clsact ingress hook. */
#include "tc_common.h"

struct {
    __uint(type, BPF_MAP_TYPE_LRU_HASH);
    __uint(max_entries, HEAVY_TABLE_SIZE);
    __type(key, struct flow_key);
    __type(value, struct flow_state);
} heavy SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, CMS_WIDTH);
    __type(key, __u32);
    __type(value, struct bucket);
} light SEC(".maps");

SEC("classifier")
int tc_elastic_ingress(struct __sk_buff *skb)
{
    struct flow_key key = {};
    int is_syn = 0;

    if (!parse_packet_tc(skb, &key, &is_syn))
        return TC_ACT_OK;
    if (key.proto != IPPROTO_UDP &&
        !(key.proto == IPPROTO_TCP && is_syn))
        return TC_ACT_OK;

    __u64 now = bpf_ktime_get_ns();
    struct flow_state *state = bpf_map_lookup_elem(&heavy, &key);
    if (state) {
        if (now - state->window_ns > ONE_SEC_NS) {
            state->window_ns = now;
            state->count = 1;
        } else {
            state->count += 1;
        }
        if (state->count > DEFAULT_THRESHOLD_PPS) {
            __sync_fetch_and_add(&state->drops, 1);
            return TC_ACT_SHOT;
        }
        return TC_ACT_OK;
    }

    __u32 idx = flow_hash(&key) & ((__u32)CMS_WIDTH - 1);
    struct bucket *bucket = bpf_map_lookup_elem(&light, &idx);
    if (!bucket)
        return TC_ACT_OK;

    if (now - bucket->window_ns > ONE_SEC_NS) {
        bucket->window_ns = now;
        bucket->count = 1;
    } else {
        bucket->count += 1;
    }

    if (bucket->count > DEFAULT_THRESHOLD_PPS) {
        struct flow_state new_state = {
            .window_ns = now,
            .count = bucket->count,
            .drops = 1,
        };
        bpf_map_update_elem(&heavy, &key, &new_state, BPF_ANY);
        return TC_ACT_SHOT;
    }

    return TC_ACT_OK;
}

char _license[] SEC("license") = "GPL";
