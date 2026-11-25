/*
 * ddos_prototype_init.c
 *
 * Sets up the hooks for one experiment configuration (exp_config.h):
 * which filter (none / native C / VM bytecode) runs at which hook
 * (lwIP hook / driver hook). Bytecode is installed only through the
 * hooks' vm_slot, which verifies it and checks it was written for that
 * hook (D9), so there is no separate vm_verify() call here.
 *
 *   ddos_prototype_init()             the config chosen at build time
 *   ddos_prototype_init_config(cfg)   any config at run time (the
 *                                     simulation runs all five in one image)
 *
 * Call before the network is brought up.
 */

#include <stdio.h>

#include "ddos_prototype.h"
#include "exp_config.h"
#include "xdp_dma_hook.h"
#include "lwip_ip4_hook.h"
#include "ddos_bytecode.h"
#include "ddos_native.h"

static const char *const s_names[EXP_NUM_CONFIGS] = {
    "C0: no filter",
    "C1: native C at lwIP hook",
    "C2: VM bytecode at lwIP hook",
    "C3: native C at driver hook",
    "C4: VM bytecode at driver hook",
};

const char *ddos_config_name(int cfg)
{
    return (cfg >= 0 && cfg < EXP_NUM_CONFIGS) ? s_names[cfg] : "invalid config";
}

static int report(const char *hook, vm_slot_result_t r, vm_verify_result_t why)
{
    if (r == VM_SLOT_OK) {
        return 0;
    }
    printf("[ddos_prototype] %s: install refused: %s (%s)\r\n",
           hook, vm_slot_strerror(r), vm_verify_strerror(why));
    return -1;
}

int ddos_prototype_init_config(int cfg)
{
    vm_verify_result_t why;
    int rc = 0;

    xdp_hook_init();          /* both hooks start with no filter */
    lwip_ip4_hook_init();

    switch (cfg) {
    case EXP_C0_NONE:
        break;
    case EXP_C1_NATIVE_LWIP:
        lwip_ip4_hook_install_native(ddos_native_ip);
        break;
    case EXP_C2_VM_LWIP:
        rc = report("lwIP hook", lwip_ip4_hook_install_vm(&ddos_program_ip, &why), why);
        break;
    case EXP_C3_NATIVE_DRIVER:
        xdp_hook_install_native(ddos_native_eth);
        break;
    case EXP_C4_VM_DRIVER:
        rc = report("driver hook", xdp_hook_install_vm(&ddos_program_eth, &why), why);
        break;
    default:
        printf("[ddos_prototype] invalid config %d\r\n", cfg);
        return -1;
    }

    if (rc == 0) {
        printf("[ddos_prototype] %s (target UDP port %u)\r\n",
               ddos_config_name(cfg), DDOS_TARGET_PORT);
    }
    return rc;
}

int ddos_prototype_init(void)
{
    return ddos_prototype_init_config(EXP_CONFIG);
}
