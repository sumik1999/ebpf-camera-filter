/* XDP deny-list maps and constant-time lookup helpers. */
#ifndef AIRPORT_EBPF_BLOCKLIST_H
#define AIRPORT_EBPF_BLOCKLIST_H

#define BLOOM_BITS 8192U
#define BLOOM_WORD_BITS 64U
#define BLOOM_WORDS (BLOOM_BITS / BLOOM_WORD_BITS)
#define BLOOM_HASHES 3

struct ipv4_lpm_key {
    __u32 prefixlen;
    __be32 address;
};

struct defense_config {
    __u32 bloom_enabled;
};

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 4096);
    __type(key, __be32);
    __type(value, __u8);
} exact_deny SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_LPM_TRIE);
    __uint(max_entries, 1024);
    __uint(map_flags, BPF_F_NO_PREALLOC);
    __type(key, struct ipv4_lpm_key);
    __type(value, __u8);
} prefix_deny SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, BLOOM_WORDS);
    __type(key, __u32);
    __type(value, __u64);
} bloom_bits SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, struct defense_config);
} defense_cfg SEC(".maps");

static __always_inline __u32 blocklist_mix(__u32 value)
{
    value ^= value >> 16;
    value *= 0x7feb352dU;
    value ^= value >> 15;
    value *= 0x846ca68bU;
    value ^= value >> 16;
    return value;
}

static __always_inline int bloom_contains(__u32 source)
{
    const __u32 seeds[BLOOM_HASHES] = {
        0x9e3779b9U,
        0x85ebca6bU,
        0xc2b2ae35U,
    };

#pragma unroll
    for (int i = 0; i < BLOOM_HASHES; i++) {
        __u32 bit = blocklist_mix(source ^ seeds[i]) & (BLOOM_BITS - 1);
        __u32 word_index = bit / BLOOM_WORD_BITS;
        __u64 *word = bpf_map_lookup_elem(&bloom_bits, &word_index);
        if (!word || !(*word & (1ULL << (bit & (BLOOM_WORD_BITS - 1)))))
            return 0;
    }
    return 1;
}

static __always_inline int source_is_blocked(__u32 source)
{
    __be32 source_be = bpf_htonl(source);
    __u8 *exact = bpf_map_lookup_elem(&exact_deny, &source_be);
    if (exact)
        return 1;

    struct ipv4_lpm_key prefix_key = {
        .prefixlen = 32,
        .address = source_be,
    };
    __u8 *prefix = bpf_map_lookup_elem(&prefix_deny, &prefix_key);
    if (prefix)
        return 1;

    __u32 zero = 0;
    struct defense_config *config =
        bpf_map_lookup_elem(&defense_cfg, &zero);
    return config && config->bloom_enabled && bloom_contains(source);
}

#endif
