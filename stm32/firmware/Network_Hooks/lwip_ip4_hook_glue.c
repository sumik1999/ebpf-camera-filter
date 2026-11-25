/*
 * lwip_ip4_hook_glue.c -- connects the late-hook filter core to lwIP's
 * LWIP_HOOK_IP4_INPUT (see lwip_ip4_hook.h for the lwipopts.h lines).
 */
#include "lwip/pbuf.h"
#include "lwip/netif.h"
#include "lwip_ip4_hook.h"

uint8_t lwip_ip4_hook_input(struct pbuf *p, struct netif *inp)
{
    (void)inp;
    if (p == NULL) {
        return 0;
    }

    /* Only the first pbuf of the chain is filtered (p->len bytes). With
     * ST's 1000-byte RX buffers the IPv4 and UDP headers are always in it
     * (D10). Bytes beyond p->len are treated as missing: a load there
     * faults and the VM DROPs (fail safe). */
    if (lwip_ip4_hook_run((const uint8_t *)p->payload, (uint32_t)p->len) == VM_VERDICT_DROP) {
        pbuf_free(p);   /* hook contract: a consumed packet is freed by the hook */
        return 1;
    }
    return 0;
}
