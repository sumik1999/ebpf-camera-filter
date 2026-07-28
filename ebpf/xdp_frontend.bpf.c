/*
 * First-stage XDP front end for the multi-layer camera defense pipeline.
 *
 * This stage performs cheap wire-format validation before skb allocation.
 * Blocklist and adaptive SYN decisions will be added in later increments.
 */
#include "common.h"
#include "blocklist.h"

SEC("xdp")
int xdp_frontend(struct xdp_md *ctx)
{
    void *data = (void *)(long)ctx->data;
    void *data_end = (void *)(long)ctx->data_end;
    struct flow_key key = {};
    int is_syn = 0;

    int result = parse_frame(data, data_end, &key, &is_syn);
    if (result == AIRPORT_PARSE_MALFORMED)
        return XDP_DROP;
    if (result != AIRPORT_PARSE_SKIP && source_is_blocked(key.src))
        return XDP_DROP;
    return XDP_PASS;
}

char _license[] SEC("license") = "GPL";
