#ifndef VM_LOADER_H
#define VM_LOADER_H

/*
 * vm_loader -- turns a received message into an installed program, so new
 * filter rules can reach a running device as DATA, with no rebuild (gap G9).
 *
 * Message format ("XVM1", all multi-byte fields little-endian):
 *
 *   offset  size  field
 *   0       4     magic "XVM1"
 *   4       1     ISA version (must be VM_ISA_VERSION)
 *   5       1     ctx: VM_CTX_ETH (driver hook) or VM_CTX_IPV4 (lwIP hook)
 *   6       2     n_insns (1 .. VM_MAX_INSNS)
 *   8       4     CRC-32 (IEEE, as zlib.crc32) of the instruction bytes
 *   12      8*n   the instructions, 8 bytes each, as in docs/ISA.md §3
 *
 * What the loader checks, in order: length, magic, version, ctx, n_insns,
 * total size, CRC. It then copies the program into one of TWO static RAM
 * buffers and installs it through the hook's install function, which runs
 * the verifier and the ctx check (vm_slot). If anything fails, the hook
 * keeps its current program.
 *
 * One loader per hook (each has its own ctx and two buffers). Double
 * buffering: a successful load switches to the other buffer for the next
 * load, so the program that is installed is never overwritten. (With one
 * shared loader, loading ETH, then IPv4, then ETH again would overwrite the
 * driver hook's installed program -- hence one loader per hook.) (On
 * the RTOS the loader runs in tcpip_thread, below the RX task's priority,
 * so the RX task never sees a program being overwritten mid-packet.)
 *
 * What it does NOT check: WHO sent the message. The verifier makes a
 * loaded program safe; authenticating the sender is future work.
 *
 * No heap: each loader is one static struct. With the embedded default of
 * VM_MAX_INSNS=64, its two instruction buffers consume 2 x 512 B = 1 KB.
 * Builds may override VM_MAX_INSNS, and the message/storage bounds follow it.
 */

#include <stdint.h>
#include "vm_isa.h"
#include "vm_verifier.h"
#include "vm_slot.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VM_LOADER_HDR_LEN   12u
#define VM_LOADER_MAX_MSG   (VM_LOADER_HDR_LEN + 8u * VM_MAX_INSNS)

typedef enum {
    VM_LOAD_OK = 0,
    VM_LOAD_ERR_SHORT,      /* shorter than the header                     */
    VM_LOAD_ERR_MAGIC,      /* not "XVM1"                                  */
    VM_LOAD_ERR_VERSION,    /* other ISA version                           */
    VM_LOAD_ERR_CTX,        /* message ctx is not this loader's hook       */
    VM_LOAD_ERR_LENGTH,     /* n_insns 0 / too big, or size doesn't match  */
    VM_LOAD_ERR_CRC,        /* instruction bytes damaged                   */
    VM_LOAD_ERR_INSTALL,    /* the hook refused it (see *slot_r, *why)     */
} vm_load_result_t;

/* A hook's install function: xdp_hook_install_vm or lwip_ip4_hook_install_vm. */
typedef vm_slot_result_t (*vm_install_fn)(const vm_program_t *prog, vm_verify_result_t *why);

typedef struct {
    vm_insn_t    insns[2][VM_MAX_INSNS];
    vm_program_t prog[2];
    uint8_t      next;          /* buffer the next load writes into     */
    uint8_t      ctx;           /* the hook this loader feeds           */
    vm_install_fn install;      /* that hook's install function         */
} vm_loader_t;

/* One loader per hook, e.g. vm_loader_init(&ld_eth, VM_CTX_ETH, xdp_hook_install_vm). */
void vm_loader_init(vm_loader_t *ld, uint8_t ctx, vm_install_fn install);

/* The ctx byte of a message (0 if too short): lets the caller pick the loader. */
uint8_t vm_loader_msg_ctx(const uint8_t *msg, uint32_t len);

/* Check, copy and install one message. slot_r and why (both optional) say
 * why an install was refused. */
vm_load_result_t vm_loader_load(vm_loader_t *ld, const uint8_t *msg, uint32_t len,
                                vm_slot_result_t *slot_r, vm_verify_result_t *why);

uint32_t vm_crc32(const uint8_t *data, uint32_t len);   /* IEEE 802.3 / zlib */
const char *vm_load_strerror(vm_load_result_t r);

#ifdef __cplusplus
}
#endif

#endif /* VM_LOADER_H */
