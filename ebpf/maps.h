#ifndef AIRPORT_EBPF_MAPS_H
#define AIRPORT_EBPF_MAPS_H

#define CMS_WIDTH 1024
#define CMS_DEPTH 4
#define CMS_BUCKETS (CMS_WIDTH * CMS_DEPTH)
#define HEAVY_TABLE_SIZE 256
#define DEFAULT_THRESHOLD_PPS 400

/* IPv4 five-tuple. Explicit padding keeps map keys deterministic. */
struct flow_key {
    unsigned int src;
    unsigned int dst;
    unsigned short sport;
    unsigned short dport;
    unsigned char proto;
    unsigned char pad[3];
};

struct bucket {
    unsigned long long window_ns;
    unsigned int count;
};

struct flow_state {
    unsigned long long window_ns;
    unsigned int count;
    unsigned int drops;
};

#endif

