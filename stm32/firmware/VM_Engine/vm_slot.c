#include "vm_slot.h"

void vm_slot_init(vm_slot_t *slot, uint8_t ctx)
{
    if (slot == NULL) {
        return;
    }
    slot->prog = NULL;
    slot->ctx = ctx;
}

vm_slot_result_t vm_slot_install(vm_slot_t *slot, const vm_program_t *prog,
                                 vm_verify_result_t *why)
{
    if (why) *why = VM_VERIFY_OK;
    if (slot == NULL || prog == NULL) {
        return VM_SLOT_ERR_NULL;
    }

    vm_verify_result_t r = vm_verify(prog);       /* rule 1: verified, always */
    if (why) *why = r;
    if (r != VM_VERIFY_OK) {
        return VM_SLOT_ERR_VERIFY;                /* slot keeps its old program */
    }
    if (prog->ctx != slot->ctx) {                 /* rule 2: right hook (Q5)   */
        return VM_SLOT_ERR_CTX;
    }

    slot->prog = prog;                            /* one atomic pointer store  */
    return VM_SLOT_OK;
}

void vm_slot_clear(vm_slot_t *slot)
{
    if (slot != NULL) {
        slot->prog = NULL;
    }
}

const char *vm_slot_strerror(vm_slot_result_t r)
{
    switch (r) {
    case VM_SLOT_OK:         return "ok";
    case VM_SLOT_ERR_NULL:   return "NULL slot or program";
    case VM_SLOT_ERR_VERIFY: return "program failed verification";
    case VM_SLOT_ERR_CTX:    return "program context does not match this hook";
    default:                 return "unknown slot error";
    }
}
