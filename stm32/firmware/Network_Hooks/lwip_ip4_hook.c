/*
 * lwip_ip4_hook.c -- filter core of the late (lwIP) hook. No lwIP code
 * here; lwip_ip4_hook_glue.c connects it to ip4_input().
 */
#include "lwip_ip4_hook.h"

static hook_t s_hook;

void lwip_ip4_hook_init(void)
{
    hook_init(&s_hook, VM_CTX_IPV4);  /* this hook delivers IPv4 packets */
}

vm_slot_result_t lwip_ip4_hook_install_vm(const vm_program_t *prog, vm_verify_result_t *why)
{
    return hook_install_vm(&s_hook, prog, why);
}

void lwip_ip4_hook_install_native(hook_native_fn fn)
{
    hook_install_native(&s_hook, fn);
}

void lwip_ip4_hook_clear(void)
{
    hook_clear(&s_hook);
}

int lwip_ip4_hook_run(const uint8_t *ip, uint32_t len)
{
    return hook_run(&s_hook, ip, len);
}

const hook_stats_t *lwip_ip4_hook_get_stats(void)
{
    return &s_hook.stats;
}

hook_filter_kind_t lwip_ip4_hook_filter_kind(void)
{
    return hook_filter_kind(&s_hook);
}
