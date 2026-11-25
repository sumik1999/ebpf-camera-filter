#ifndef LWIP_IP4_HOOK_H
#define LWIP_IP4_HOOK_H

/*
 * lwip_ip4_hook -- the LATE hook (baseline): the same filters, run at the
 * start of lwIP's ip4_input(), after the frame has crossed the
 * tcpip_thread mailbox and ethernet_input() has removed the Ethernet
 * header. So pkt[0] is the IPv4 header (programs must be VM_CTX_IPV4).
 *
 * Registration in lwipopts.h (D7: never #include lwIP headers there):
 *
 *     #define LWIP_HOOK_FILENAME "lwip_ip4_hook.h"
 *     #define LWIP_HOOK_IP4_INPUT(p, inp) lwip_ip4_hook_input((p), (inp))
 *
 * This header is then included by lwIP's ip4.c, after lwIP's own headers,
 * so it must not include lwIP headers itself; it only forward-declares
 * struct pbuf / struct netif.
 *
 * Split into two files so the filter logic can be tested without lwIP:
 *   lwip_ip4_hook.c       filter core (no lwIP code)
 *   lwip_ip4_hook_glue.c  lwip_ip4_hook_input(): pbuf -> core, pbuf_free on DROP
 */

#include <stdint.h>
#include "hook_common.h"

#ifdef __cplusplus
extern "C" {
#endif

void lwip_ip4_hook_init(void);
vm_slot_result_t lwip_ip4_hook_install_vm(const vm_program_t *prog, vm_verify_result_t *why);
void lwip_ip4_hook_install_native(hook_native_fn fn);   /* config C1 */
void lwip_ip4_hook_clear(void);

/* Filter core: `ip` points at the IPv4 header, `len` bytes are readable. */
int lwip_ip4_hook_run(const uint8_t *ip, uint32_t len);

const hook_stats_t *lwip_ip4_hook_get_stats(void);
hook_filter_kind_t lwip_ip4_hook_filter_kind(void);

/* lwIP hook contract: return 0 = continue ip4_input() (PASS);
 * non-zero = packet consumed, and the hook has already freed it (DROP). */
struct pbuf;
struct netif;
uint8_t lwip_ip4_hook_input(struct pbuf *p, struct netif *inp);

#ifdef __cplusplus
}
#endif

#endif /* LWIP_IP4_HOOK_H */
