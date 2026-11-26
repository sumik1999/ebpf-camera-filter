/*
 * xdp_dma_hook.c -- early (driver-layer) hook. See xdp_dma_hook.h for
 * where ST's ethernetif.c calls it.
 */
#include "xdp_dma_hook.h"

static hook_t s_hook;
static filter_manager_t *s_metadata;
static xdp_tick_fn s_tick_now;
static void *s_tick_opaque;

void xdp_hook_init(void)
{
    hook_init(&s_hook, VM_CTX_ETH);   /* this hook delivers Ethernet frames */
    s_metadata = (filter_manager_t *)0;
    s_tick_now = (xdp_tick_fn)0;
    s_tick_opaque = (void *)0;
}

vm_slot_result_t xdp_hook_install_vm(const vm_program_t *prog, vm_verify_result_t *why)
{
    vm_slot_result_t result=hook_install_vm(&s_hook, prog, why);
    if (result==VM_SLOT_OK) s_metadata=(filter_manager_t *)0;
    return result;
}

void xdp_hook_install_native(hook_native_fn fn)
{
    hook_install_native(&s_hook, fn);
    s_metadata=(filter_manager_t *)0;
}

void xdp_hook_clear(void)
{
    hook_clear(&s_hook);
    s_metadata=(filter_manager_t *)0;
}

int xdp_hook_select_metadata(filter_manager_t *manager,
                             xdp_tick_fn tick_now, void *tick_opaque)
{
    if (manager==NULL || filter_manager_active(manager)==NULL) return 0;
    s_tick_now=tick_now;
    s_tick_opaque=tick_opaque;
    s_metadata=manager;
    return 1;
}

int xdp_hook_run(const uint8_t *frame, uint16_t len)
{
    filter_manager_t *manager=s_metadata;
    if (manager==NULL) return hook_run(&s_hook, frame, (uint32_t)len);

    s_hook.stats.rx_total++;
    uint32_t tick=s_tick_now?s_tick_now(s_tick_opaque):0u;
    int verdict=filter_manager_run(manager,frame,(uint32_t)len,tick,NULL);
    if (verdict==VM_VERDICT_PASS) {
        s_hook.stats.rx_passed++;
        return VM_VERDICT_PASS;
    }
    s_hook.stats.rx_dropped++;
    return VM_VERDICT_DROP;
}

const hook_stats_t *xdp_hook_get_stats(void)
{
    return &s_hook.stats;
}

hook_filter_kind_t xdp_hook_filter_kind(void)
{
    return s_metadata?HOOK_FILTER_META:hook_filter_kind(&s_hook);
}
