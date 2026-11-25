#ifndef XDP_DMA_HOOK_H
#define XDP_DMA_HOOK_H

/*
 * xdp_dma_hook -- the EARLY hook: runs on the raw Ethernet frame inside
 * the Ethernet driver's RX path, before lwIP sees the frame.
 *
 * v1 (24 Nov 2026) is a pure function with no HAL, lwIP or FreeRTOS code.
 * ST's ethernetif.c keeps owning all three HAL RX callbacks and the
 * RX_POOL buffers; it calls xdp_hook_run() from its own
 * HAL_ETH_RxLinkCallback(). This replaces v0, which redefined ST's
 * callbacks and its buffer pool (fixes D2, D3, D4, D6; D8 counter renamed).
 *
 * Where it runs: HAL_ETH_RxLinkCallback() is called by HAL_ETH_ReadData()
 * in the RX task (ethernetif_input, osPriorityRealtime), NOT in the ISR.
 * The ISR only gives RxPktSemaphore.
 *
 * Integration into ST's ethernetif.c (v2.1.0), at the top of
 * HAL_ETH_RxLinkCallback(), after `p` is computed from `buff`:
 *
 *     // XDP: early filter on the first segment of each frame
 *     if (*ppStart == NULL && xdp_hook_run(buff, Length) == VM_VERDICT_DROP) {
 *         pbuf_free(p);   // custom pbuf -> back to RX_POOL
 *         return;         // nothing linked: lwIP never sees the frame
 *     }
 *
 * After a DROP, HAL_ETH_ReadData() returns HAL_OK with a NULL buffer, so
 * the RX loop must keep draining on the return status (D5). If a frame
 * spans two descriptors (D14), only the first segment is filtered; when it
 * is dropped, the HAL skips the later segments and reuses their buffers.
 */

#include <stdint.h>
#include "hook_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Call once at start-up, before the MAC is started. No filter installed. */
void xdp_hook_init(void);

/* Install a bytecode program. It must be VM_CTX_ETH and pass vm_verify(),
 * otherwise it is refused and the previous filter stays. */
vm_slot_result_t xdp_hook_install_vm(const vm_program_t *prog, vm_verify_result_t *why);

/* Use a native C filter instead (configs C3), or NULL for none. */
void xdp_hook_install_native(hook_native_fn fn);

void xdp_hook_clear(void);

/* Per frame: returns VM_VERDICT_PASS or VM_VERDICT_DROP. `frame` points at
 * the destination MAC; `len` is the bytes the DMA wrote to this buffer. */
int xdp_hook_run(const uint8_t *frame, uint16_t len);

const hook_stats_t *xdp_hook_get_stats(void);
hook_filter_kind_t xdp_hook_filter_kind(void);

#ifdef __cplusplus
}
#endif

#endif /* XDP_DMA_HOOK_H */
