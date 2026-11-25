/* sim_app.c -- the application side of the simulated firmware:
 *   - UDP echo server on port 7 (like ST's example, which uses netconn;
 *     here the raw API, so it also works without an RTOS);
 *   - UDP control port 9000: receives an XVM1 message (tools/asm/bcgen.py)
 *     and installs it with vm_loader into the hook named by its ctx, then
 *     replies "OK ..." or "ERR <reason>". This is how new rules reach a
 *     running device without a rebuild (gap G9). The sender is NOT
 *     authenticated: that is stated future work. */
#include <stdio.h>
#include <string.h>
#include "lwip/udp.h"
#include "sim_app.h"
#include "vm_loader.h"
#include "xdp_dma_hook.h"
#include "lwip_ip4_hook.h"

#define CTRL_PORT 9000u

uint32_t g_echo_rx, g_ctrl_ok, g_ctrl_refused;

static vm_loader_t s_ld_eth, s_ld_ip;           /* one loader per hook */
static uint8_t s_msg[VM_LOADER_MAX_MSG];

static void echo_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                      const ip_addr_t *addr, u16_t port)
{
    (void)arg;
    g_echo_rx++;
    udp_sendto(pcb, p, addr, port);
    pbuf_free(p);
}

static void ctrl_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                      const ip_addr_t *addr, u16_t port)
{
    char reply[96];
    vm_slot_result_t sr = VM_SLOT_OK;
    vm_verify_result_t why = VM_VERIFY_OK;
    vm_load_result_t r = VM_LOAD_ERR_SHORT;
    (void)arg;

    uint16_t len = (p->tot_len <= sizeof s_msg) ? p->tot_len : 0;
    if (len > 0 && pbuf_copy_partial(p, s_msg, len, 0) == len) {
        uint8_t ctx = vm_loader_msg_ctx(s_msg, len);
        vm_loader_t *ld = (ctx == VM_CTX_IPV4) ? &s_ld_ip : &s_ld_eth;
        r = vm_loader_load(ld, s_msg, len, &sr, &why);
    }
    pbuf_free(p);

    if (r == VM_LOAD_OK) {
        g_ctrl_ok++;
        snprintf(reply, sizeof reply, "OK installed in %s hook",
                 vm_loader_msg_ctx(s_msg, len) == VM_CTX_IPV4 ? "lwIP" : "driver");
    } else {
        g_ctrl_refused++;
        snprintf(reply, sizeof reply, "ERR %s%s%s", vm_load_strerror(r),
                 r == VM_LOAD_ERR_INSTALL ? ": " : "",
                 r == VM_LOAD_ERR_INSTALL ? vm_verify_strerror(why) : "");
    }
#if !SIM_RTOS
    printf("[ctrl] %s\n", reply);   /* (RTOS build: newlib printf is not task-safe here) */
#endif
    struct pbuf *q = pbuf_alloc(PBUF_TRANSPORT, (u16_t)strlen(reply), PBUF_RAM);
    if (q != NULL) {
        memcpy(q->payload, reply, strlen(reply));
        udp_sendto(pcb, q, addr, port);
        pbuf_free(q);
    }
}

static void bind_or_die(udp_recv_fn fn, u16_t port)
{
    struct udp_pcb *pcb = udp_new();
    if (pcb == NULL || udp_bind(pcb, IP_ADDR_ANY, port) != ERR_OK) sim_fatal("udp bind");
    udp_recv(pcb, fn, NULL);
}

void sim_app_init(void)
{
    vm_loader_init(&s_ld_eth, VM_CTX_ETH, xdp_hook_install_vm);
    vm_loader_init(&s_ld_ip, VM_CTX_IPV4, lwip_ip4_hook_install_vm);
    bind_or_die(echo_recv, 7);
    bind_or_die(ctrl_recv, CTRL_PORT);
}
