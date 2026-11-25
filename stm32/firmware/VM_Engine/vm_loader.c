/* vm_loader.c -- see vm_loader.h for the message format and the checks. */
#include "vm_loader.h"

uint32_t vm_crc32(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int k = 0; k < 8; k++) {             /* bit by bit: no 1 KB table in flash */
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

static uint32_t le16(const uint8_t *b) { return (uint32_t)b[0] | ((uint32_t)b[1] << 8); }
static uint32_t le32(const uint8_t *b) { return le16(b) | (le16(b + 2) << 16); }

void vm_loader_init(vm_loader_t *ld, uint8_t ctx, vm_install_fn install)
{
    if (ld != NULL) {
        ld->next = 0;
        ld->ctx = ctx;
        ld->install = install;
    }
}

uint8_t vm_loader_msg_ctx(const uint8_t *msg, uint32_t len)
{
    return (msg != NULL && len >= VM_LOADER_HDR_LEN) ? msg[5] : 0u;
}

vm_load_result_t vm_loader_load(vm_loader_t *ld, const uint8_t *msg, uint32_t len,
                                vm_slot_result_t *slot_r, vm_verify_result_t *why)
{
    if (slot_r) *slot_r = VM_SLOT_OK;
    if (why) *why = VM_VERIFY_OK;
    if (ld == NULL || ld->install == (vm_install_fn)0 || msg == NULL || len < VM_LOADER_HDR_LEN)
        return VM_LOAD_ERR_SHORT;
    if (msg[0] != 'X' || msg[1] != 'V' || msg[2] != 'M' || msg[3] != '1') return VM_LOAD_ERR_MAGIC;
    if (msg[4] != VM_ISA_VERSION) return VM_LOAD_ERR_VERSION;

    uint8_t ctx = msg[5];
    if (ctx != ld->ctx) return VM_LOAD_ERR_CTX;   /* (the slot checks again) */

    uint32_t n = le16(msg + 6);
    if (n == 0u || n > VM_MAX_INSNS || len != VM_LOADER_HDR_LEN + 8u * n) return VM_LOAD_ERR_LENGTH;

    const uint8_t *body = msg + VM_LOADER_HDR_LEN;
    if (vm_crc32(body, 8u * n) != le32(msg + 8)) return VM_LOAD_ERR_CRC;

    /* Decode field by field (endian-independent), into the free buffer. */
    vm_insn_t *dst = ld->insns[ld->next];
    for (uint32_t i = 0; i < n; i++) {
        const uint8_t *b = body + 8u * i;
        dst[i].opcode  = b[0];
        dst[i].dst_reg = (uint8_t)(b[1] & 0x0Fu);
        dst[i].src_reg = (uint8_t)(b[1] >> 4);
        dst[i].offset  = (int16_t)le16(b + 2);
        dst[i].imm     = (int32_t)le32(b + 4);
    }
    vm_program_t *prog = &ld->prog[ld->next];
    prog->insns = dst;
    prog->n_insns = (uint16_t)n;
    prog->ctx = ctx;

    vm_slot_result_t r = ld->install(prog, why);      /* verify + ctx check (vm_slot) */
    if (slot_r) *slot_r = r;
    if (r != VM_SLOT_OK) return VM_LOAD_ERR_INSTALL;

    ld->next ^= 1u;                               /* never overwrite the installed one */
    return VM_LOAD_OK;
}

const char *vm_load_strerror(vm_load_result_t r)
{
    switch (r) {
    case VM_LOAD_OK:          return "ok";
    case VM_LOAD_ERR_SHORT:   return "message shorter than the header";
    case VM_LOAD_ERR_MAGIC:   return "bad magic (not XVM1)";
    case VM_LOAD_ERR_VERSION: return "unsupported ISA version";
    case VM_LOAD_ERR_CTX:     return "program is for another hook";
    case VM_LOAD_ERR_LENGTH:  return "bad instruction count or message length";
    case VM_LOAD_ERR_CRC:     return "CRC mismatch (damaged program)";
    case VM_LOAD_ERR_INSTALL: return "hook refused the program";
    default:                  return "unknown load error";
    }
}
