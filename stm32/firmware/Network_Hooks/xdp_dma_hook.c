/*
 * xdp_dma_hook.c -- early (driver-layer) hook. See xdp_dma_hook.h for
 * where ST's ethernetif.c calls it.
 */
#include "xdp_dma_hook.h"

static hook_t s_hook;

void xdp_hook_init(void)
{
    hook_init(&s_hook, VM_CTX_ETH);   /* this hook delivers Ethernet frames */
}

vm_slot_result_t xdp_hook_install_vm(const vm_program_t *prog, vm_verify_result_t *why)
{
    return hook_install_vm(&s_hook, prog, why);
}

void xdp_hook_install_native(hook_native_fn fn)
{
    hook_install_native(&s_hook, fn);
}

void xdp_hook_clear(void)
{
    hook_clear(&s_hook);
}

int xdp_hook_run(const uint8_t *frame, uint16_t len)
{
    return hook_run(&s_hook, frame, (uint32_t)len);
}

const hook_stats_t *xdp_hook_get_stats(void)
{
    return &s_hook.stats;
}

hook_filter_kind_t xdp_hook_filter_kind(void)
{
    return hook_filter_kind(&s_hook);
}
