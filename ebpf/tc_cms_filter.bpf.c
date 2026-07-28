/* Count-Min Sketch flood filter for a TC clsact ingress hook. */
#include "tc_common.h"

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, CMS_BUCKETS);
    __type(key, __u32);
    __type(value, struct bucket);
} cms SEC(".maps");

SEC("classifier")
int tc_cms_ingress(struct __sk_buff *skb)
{
    struct flow_key key = {};
    int is_syn = 0;

    if (!parse_packet_tc(skb, &key, &is_syn))
        return TC_ACT_OK;
    if (key.proto != IPPROTO_UDP &&
        !(key.proto == IPPROTO_TCP && is_syn))
        return TC_ACT_OK;

    __u64 now = bpf_ktime_get_ns();
    __u32 h = flow_hash(&key);
    __u32 min_count = 0xffffffffU;

#pragma unroll
    for (int depth = 0; depth < CMS_DEPTH; depth++) {
        __u32 idx = (h + (__u32)depth * 0x9e3779b9U) &
                    ((__u32)CMS_WIDTH - 1);
        idx += (__u32)depth * CMS_WIDTH;

        struct bucket *bucket = bpf_map_lookup_elem(&cms, &idx);
        if (!bucket)
            continue;

        if (now - bucket->window_ns > ONE_SEC_NS) {
            bucket->window_ns = now;
            bucket->count = 1;
        } else {
            bucket->count += 1;
        }
        if (bucket->count < min_count)
            min_count = bucket->count;
    }

    return min_count > DEFAULT_THRESHOLD_PPS ? TC_ACT_SHOT : TC_ACT_OK;
}

char _license[] SEC("license") = "GPL";
