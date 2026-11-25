/*
 * test_hooks.c -- host tests for the two hooks (driver hook and lwIP hook
 * filter core). No HAL and no lwIP are needed: the hooks are pure C.
 */
#include "test_common.h"

#include "xdp_dma_hook.h"
#include "lwip_ip4_hook.h"
#include "ddos_bytecode.h"

static int s_native_calls;
static int native_drop_all(const uint8_t *pkt, uint32_t len)
{
    (void)pkt; (void)len;
    s_native_calls++;
    return VM_VERDICT_DROP;
}
static int native_bad_verdict(const uint8_t *pkt, uint32_t len)
{
    (void)pkt; (void)len;
    return 7;   /* neither PASS nor DROP */
}

static void test_driver_hook(void)
{
    uint8_t f[256];
    vm_verify_result_t why;
    const hook_stats_t *st = xdp_hook_get_stats();
    frame_spec_t attack = { 0x0800, 5, 17, 0x4000, 40000, DDOS_TARGET_PORT, 32 };
    frame_spec_t dns    = { 0x0800, 5, 17, 0x4000, 40000, 53, 32 };
    uint8_t a[256];
    uint16_t na = (uint16_t)build_frame(a, sizeof a, &attack);
    uint16_t nd = (uint16_t)build_frame(f, sizeof f, &dns);

    printf("[hooks] driver hook (xdp_hook_run)\n");
    xdp_hook_init();
    CHECK(xdp_hook_run(a, na) == VM_VERDICT_PASS && st->rx_no_filter_pass == 1 && st->rx_total == 1,
          "no filter installed: frame passes, counted as rx_no_filter_pass (D8)");

    CHECK(xdp_hook_install_vm(&ddos_program_ip, &why) == VM_SLOT_ERR_CTX,
          "driver hook refuses the IPv4-context program");
    CHECK(xdp_hook_install_vm(&ddos_program_eth, &why) == VM_SLOT_OK, "driver hook accepts the ETH program");

    CHECK(xdp_hook_run(a, na) == VM_VERDICT_DROP, "attack frame dropped by the VM at the driver hook");
    CHECK(xdp_hook_run(f, nd) == VM_VERDICT_PASS, "DNS frame passed by the VM at the driver hook");
    CHECK(xdp_hook_run(a, 20) == VM_VERDICT_DROP, "truncated IPv4 frame dropped (fail safe)");
    CHECK(st->rx_total == 4 && st->rx_dropped == 2 && st->rx_passed == 1 && st->rx_no_filter_pass == 1,
          "counters add up: total = passed + dropped + no_filter_pass");

    s_native_calls = 0;
    xdp_hook_install_native(native_drop_all);
    CHECK(xdp_hook_run(f, nd) == VM_VERDICT_DROP && s_native_calls == 1,
          "native filter replaces the VM program (DNS now dropped by the stub)");

    CHECK(xdp_hook_install_vm(&ddos_program_ip, &why) == VM_SLOT_ERR_CTX &&
          xdp_hook_run(f, nd) == VM_VERDICT_DROP && s_native_calls == 2,
          "refused VM install leaves the native filter in place");

    CHECK(xdp_hook_install_vm(&ddos_program_eth, &why) == VM_SLOT_OK &&
          xdp_hook_run(f, nd) == VM_VERDICT_PASS && s_native_calls == 2,
          "installing a VM program removes the native filter");

    xdp_hook_install_native(native_bad_verdict);
    uint32_t dropped = st->rx_dropped;
    CHECK(xdp_hook_run(f, nd) == VM_VERDICT_DROP && st->rx_dropped == dropped + 1,
          "a filter verdict other than PASS is treated as DROP");

    xdp_hook_clear();
    CHECK(xdp_hook_run(a, na) == VM_VERDICT_PASS, "after clear, every frame passes");
}

static void test_lwip_hook_core(void)
{
    uint8_t f[256];
    vm_verify_result_t why;
    const hook_stats_t *st = lwip_ip4_hook_get_stats();
    frame_spec_t attack = { 0x0800, 5, 17, 0x4000, 40000, DDOS_TARGET_PORT, 32 };
    frame_spec_t echo   = { 0x0800, 5, 17, 0x4000, 40000, 7, 32 };

    printf("[hooks] lwIP hook filter core (lwip_ip4_hook_run)\n");
    lwip_ip4_hook_init();
    CHECK(lwip_ip4_hook_install_vm(&ddos_program_eth, &why) == VM_SLOT_ERR_CTX,
          "lwIP hook refuses the ETH-context program");
    CHECK(lwip_ip4_hook_install_vm(&ddos_program_ip, &why) == VM_SLOT_OK,
          "lwIP hook accepts the IPv4 program");

    /* The lwIP hook sees the packet from the IPv4 header on. */
    uint32_t n = build_frame(f, sizeof f, &attack);
    CHECK(lwip_ip4_hook_run(f + ETH_HDR, n - ETH_HDR) == VM_VERDICT_DROP,
          "attack packet dropped at the lwIP hook");
    n = build_frame(f, sizeof f, &echo);
    CHECK(lwip_ip4_hook_run(f + ETH_HDR, n - ETH_HDR) == VM_VERDICT_PASS,
          "UDP echo (port 7) packet passed at the lwIP hook");
    CHECK(st->rx_total == 2 && st->rx_dropped == 1 && st->rx_passed == 1,
          "lwIP hook counters add up");

    /* The two hooks are independent. */
    xdp_hook_init();
    CHECK(lwip_ip4_hook_run(f + ETH_HDR, n - ETH_HDR) == VM_VERDICT_PASS && st->rx_total == 3,
          "re-initialising the driver hook does not touch the lwIP hook");
}

void test_hooks(void)
{
    test_driver_hook();
    test_lwip_hook_core();
}

/* EXP_CONFIG C0-C4: in each config exactly the expected hook filters,
 * with the expected kind of filter, and it drops the attack frame. */
#include "ddos_prototype.h"
#include "exp_config.h"

void test_exp_configs(void)
{
    uint8_t f[256];
    frame_spec_t attack = { 0x0800, 5, 17, 0x4000, 40000, DDOS_TARGET_PORT, 32 };
    frame_spec_t dns    = { 0x0800, 5, 17, 0x4000, 40000, 53, 32 };
    static const struct {
        hook_filter_kind_t driver, lwip;
    } want[EXP_NUM_CONFIGS] = {
        { HOOK_FILTER_NONE,   HOOK_FILTER_NONE   },   /* C0 */
        { HOOK_FILTER_NONE,   HOOK_FILTER_NATIVE },   /* C1 */
        { HOOK_FILTER_NONE,   HOOK_FILTER_VM     },   /* C2 */
        { HOOK_FILTER_NATIVE, HOOK_FILTER_NONE   },   /* C3 */
        { HOOK_FILTER_VM,     HOOK_FILTER_NONE   },   /* C4 */
    };
    printf("[exp] EXP_CONFIG C0-C4 set up the right filter at the right hook\n");

    for (int c = 0; c < EXP_NUM_CONFIGS; c++) {
        char msg[96];
        CHECK(ddos_prototype_init_config(c) == 0, "config initialises");
        snprintf(msg, sizeof msg, "%s: filter kinds at driver/lwIP hook", ddos_config_name(c));
        CHECK(xdp_hook_filter_kind() == want[c].driver && lwip_ip4_hook_filter_kind() == want[c].lwip, msg);

        uint32_t n = build_frame(f, sizeof f, &attack);
        int d = xdp_hook_run(f, (uint16_t)n);
        int l = lwip_ip4_hook_run(f + ETH_HDR, n - ETH_HDR);
        int dropped_by = (d == VM_VERDICT_DROP) ? 1 : (l == VM_VERDICT_DROP) ? 2 : 0;
        int expect = (want[c].driver != HOOK_FILTER_NONE) ? 1 : (want[c].lwip != HOOK_FILTER_NONE) ? 2 : 0;
        snprintf(msg, sizeof msg, "%s: attack dropped at the expected hook", ddos_config_name(c));
        CHECK(dropped_by == expect, msg);

        n = build_frame(f, sizeof f, &dns);
        snprintf(msg, sizeof msg, "%s: DNS passes both hooks", ddos_config_name(c));
        CHECK(xdp_hook_run(f, (uint16_t)n) == VM_VERDICT_PASS &&
              lwip_ip4_hook_run(f + ETH_HDR, n - ETH_HDR) == VM_VERDICT_PASS, msg);
    }
    CHECK(ddos_prototype_init_config(5) == -1 && ddos_prototype_init_config(-1) == -1,
          "configs outside 0..4 are refused (C5/C6 need hardware)");
    CHECK(EXP_CONFIG == EXP_C4_VM_DRIVER, "default build config is C4 (the proposed system)");
    xdp_hook_init();
    lwip_ip4_hook_init();
}
