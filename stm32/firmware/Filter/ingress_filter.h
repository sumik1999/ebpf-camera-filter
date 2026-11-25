#ifndef INGRESS_FILTER_H
#define INGRESS_FILTER_H

#include <stdint.h>
#include "filter_policy.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FILTER_REASON_PASS = 0,
    FILTER_REASON_MALFORMED,
    FILTER_REASON_UNSUPPORTED,
    FILTER_REASON_IPV6,
    FILTER_REASON_VLAN,
    FILTER_REASON_FRAGMENT,
    FILTER_REASON_PROTOCOL,
    FILTER_REASON_SERVICE,
    FILTER_REASON_COUNT
} filter_reason_t;

typedef struct {
    uint32_t rx_total;
    uint32_t rx_passed;
    uint32_t rx_dropped;
    uint32_t reason[FILTER_REASON_COUNT];
} ingress_filter_stats_t;

/* Initialize the singleton used by the driver ingress hook. The initial
 * monitor policy parses and counts but preserves all existing verdicts. */
void ingress_filter_init(void);

/* Configuration is copied into fixed storage. Call while RX is stopped;
 * authenticated A/B runtime policy replacement is a later milestone.
 * Returns 0 when the policy is invalid and leaves the old policy active. */
int ingress_filter_configure(const filter_policy_t *policy);

/* Returns 1 for PASS and 0 for DROP. */
int ingress_filter_run(const uint8_t *frame, uint32_t len);

const ingress_filter_stats_t *ingress_filter_get_stats(void);
const filter_policy_t *ingress_filter_get_policy(void);

#ifdef __cplusplus
}
#endif

#endif /* INGRESS_FILTER_H */
