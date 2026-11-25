#include "filter_policy.h"

#include <stddef.h>
#include <string.h>

#define IPPROTO_TCP_VALUE 6u
#define IPPROTO_UDP_VALUE 17u

void filter_policy_monitor(filter_policy_t *policy)
{
    if (policy != NULL) {
        memset(policy, 0, sizeof(*policy));
    }
}

int filter_policy_validate(const filter_policy_t *policy)
{
    if (policy == NULL || policy->vlan_count > FILTER_MAX_VLANS ||
        policy->service_count > FILTER_MAX_SERVICES) {
        return 0;
    }
    for (uint32_t i = 0; i < policy->vlan_count; i++) {
        if (policy->allowed_vlans[i] > 4095u) {
            return 0;
        }
    }
    for (uint32_t i = 0; i < policy->service_count; i++) {
        const filter_service_t *service = &policy->services[i];
        if ((service->protocol != IPPROTO_TCP_VALUE &&
             service->protocol != IPPROTO_UDP_VALUE) ||
            service->port == 0u) {
            return 0;
        }
    }
    return 1;
}
