# Code walkthrough, part 1: the VM engine

For Nishant and Kiran. Read this with the code open beside it. Part 1 (the VM engine) takes about
45 minutes; **part 2** (the slot, the hooks, the native filter, the configs and the emulator, written
24 Nov) starts after part 1's exercises and takes about 40 minutes. Line numbers are as of 24 Nov.

**Read the files in this order:**
1. `firmware/VM_Engine/vm_isa.h`: the "dictionary" (what an instruction is).
2. `firmware/VM_Engine/vm_interpreter.c`: the "engine" (runs a program on a packet).
3. `firmware/VM_Engine/vm_verifier.c`: the "inspector" (checks a program before it may run).
4. `firmware/DDoS_Prototype/ddos_bytecode.h`: the "rulebook" (our DDoS filter).
5. `tests/host/test_vm.c`: the "proof that it works" (190 automatic checks with part 2's tests).

Section 6 has six short exercises (with answers) to test yourselves.

---

## 0. The big picture in five lines

1. A **filter program** is an array of 8-byte **instructions** (the "bytecode").
2. Before it may be used, `vm_verify()` checks the program **once**.
3. For every received packet, `vm_interpreter_run(program, packet, length, stats)` runs it.
4. The result is a **verdict**: `VM_VERDICT_PASS` (1) or `VM_VERDICT_DROP` (0).
5. No heap memory is used anywhere. Everything is fixed-size, static or on the stack.

| File | Lines | Role |
|---|---|---|
| `vm_isa.h` | 148 | Instruction format, opcodes, limits, program type |
| `vm_interpreter.h` / `.c` | 47 / 158 | Runs programs |
| `vm_verifier.h` / `.c` | 61 / 144 | Checks programs |
| `ddos_bytecode.h` | ~110 | The DDoS filter, in two versions (one per hook) |
| `test_vm.c` | 816 | Host tests (VM part; part 2 adds `test_hooks.c`, `test_native.c`) |

---

## 1. `vm_isa.h`: the dictionary

### 1.1 One instruction (line 30)
```c
typedef struct __attribute__((packed)) {
    uint8_t  opcode;       /* what to do                                  */
    uint8_t  dst_reg : 4;  /* destination register (result box)           */
    uint8_t  src_reg : 4;  /* source register (second operand / load base) */
    int16_t  offset;       /* jump distance, or packet position for loads  */
    int32_t  imm;          /* a constant number ("immediate")              */
} vm_insn_t;
_Static_assert(sizeof(vm_insn_t) == 8, "...");   /* line 38: the compiler refuses to build if it isn't 8 bytes */
```
- **`packed`** means no gaps between the fields: exactly 8 bytes.
- **`: 4`** is a *bit-field*: `dst_reg` and `src_reg` share one byte, 4 bits each. That allows values
  0–15, but only 0–7 are legal; the verifier checks this.
- **Byte layout in memory:** byte 0 = opcode; byte 1 = dst (low 4 bits) + src (high 4 bits); bytes 2–3
  = offset; bytes 4–7 = imm. This is the same on the Mac and on the Cortex-M33 (checked; see
  `docs/ISA.md` §3).

### 1.2 Limits (lines 43–47)
| Name | Value | Meaning |
|---|---|---|
| `VM_ISA_VERSION` | 1 | Version of this instruction set (frozen 28 Oct) |
| `VM_NUM_REGS` | 8 | Registers r0–r7 |
| `VM_MAX_INSNS` | 64 by default | Longest allowed program; build-time configurable |
| `VM_MAX_STEPS` | 2000 | Safety budget in the interpreter (can never be reached by a verified program) |

### 1.3 How opcodes are numbered (lines 50 onwards)
Every opcode = **(operation << 3) | class**. The low 3 bits are the *class* (the family of the
instruction); the upper bits pick the instruction inside the family.

| Class (low 3 bits) | Family | Examples |
|---|---|---|
| 0 `LD` | read from packet | `LDPKT_B` (1 byte), `_H` (2), `_W` (4) |
| 1 `ALU_K` | arithmetic with a constant | `ADD_K`, `AND_K`, `RSH_K`, … |
| 2 `ALU_X` | arithmetic with a register | `ADD_X`, … |
| 3 `JMP_K` | jump, compare with a constant | `JMP`, `JEQ_K`, `JNE_K`, `JGT_K`, … |
| 4 `JMP_X` | jump, compare with a register | `JEQ_X`, … |
| 5 `RET` | return a verdict | `RET` |
| 6 `MISC` | moves | `MOV_K`, `MOV_X` |
| 7 | **reserved** (`VM_CLASS_RESERVED`) for ISA v2 | none yet |

Example: `VM_OP_JNE_K = (2 << 3) | 3 = 0x13`. "Operation 2 in the jump-with-constant family" means
"jump if not equal". All 33 opcodes and their hex values are listed in `docs/ISA.md` §4.

### 1.4 Verdicts, contexts, programs (lines 126–142)
```c
#define VM_VERDICT_DROP   0
#define VM_VERDICT_PASS   1
#define VM_CTX_ETH    1u   /* program expects an Ethernet frame (driver hook) */
#define VM_CTX_IPV4   2u   /* program expects an IPv4 packet (lwIP hook)      */
typedef struct {
    const vm_insn_t *insns;   /* pointer to the instruction array */
    uint16_t         n_insns; /* how many instructions            */
    uint8_t          ctx;     /* which hook it was written for    */
} vm_program_t;
```
The `ctx` exists because the two hooks see the packet at different starting points (14 bytes apart).
A program written for one would read the wrong bytes on the other.

**Check yourself:** what is the opcode of `LDPKT_W`, and which class is it in? (Exercise 3.)

---

## 2. `vm_interpreter.c`: the engine

### 2.1 The registers (line 6)
```c
typedef struct { uint32_t r[VM_NUM_REGS]; } vm_regs_t;   /* 8 × 32-bit = 32 bytes, on the stack */
```
At the start of every run all 8 boxes are set to 0 (lines 38–45), with 8 explicit stores. (Until
24 Nov this was `regs = {{0}}`, which GCC turned into a slow `memset` call; see part 2, §11.4.)

### 2.2 The bounds check (line 10)
```c
static inline int pkt_bounds_ok(uint32_t pkt_len, uint32_t addr_off, uint32_t access_size)
{
    if (access_size > pkt_len) return 0;                 /* packet smaller than the read itself */
    if (addr_off > (pkt_len - access_size)) return 0;    /* read would go past the end         */
    return 1;
}
```
**Why not simply `addr_off + access_size <= pkt_len`?** Because adding can **overflow**. If `addr_off`
is 0xFFFFFFFF (e.g. from a negative offset), adding 2 wraps around to 1, and the check would wrongly
pass. Subtracting from `pkt_len` (after making sure that's safe) can't overflow. This is a classic
safe-C pattern.

### 2.3 The main loop (line 32 onwards), in plain words
```
vm_interpreter_run(prog, pkt_data, pkt_len, stats):
    if prog / insns / pkt_data is NULL, or n_insns == 0  → DROP        (fail safe)
    pc = 0, steps = 0, all registers = 0
    forever:                                                     (line 53)
        if pc >= n_insns                → DROP  (aborted_oob)    (line 54: can't happen if verified)
        if ++steps > 2000               → DROP  (aborted_budget) (line 60: can't happen if verified)
        ins = prog->insns[pc]           (fetch: copy 8 bytes)
        next_pc = pc + 1                (default: next line)     (line 67)
        switch (ins.opcode):            (decode: GCC turns this into ONE jump table, "TBH")
            loads    → bounds check, read 1/2/4 bytes big-endian into R[dst]
            ALU      → R[dst] = R[dst] (op) imm   or   (op) R[src]
            MOV      → R[dst] = imm  or  R[src]
            branches → if condition: next_pc = pc + 1 + offset
            RET      → return PASS if imm == 1, else DROP
            unknown  → DROP (aborted_oob)                        (line 150: can't happen if verified)
        pc = next_pc                                             (line 156)
```

### 2.4 One case in detail: `LDPKT_H` (line 81)
```c
case VM_OP_LDPKT_H: {
    uint32_t off = regs.r[ins.src_reg] + (int32_t)ins.offset;   /* address = base register + offset */
    if (!pkt_bounds_ok(pkt_len, off, 2u)) {                     /* are 2 bytes available?           */
        local_stats.aborted_oob++;
        return vm_finish(stats, &local_stats, steps, VM_VERDICT_DROP);  /* no → fail safe           */
    }
    regs.r[ins.dst_reg] = ((uint32_t)pkt_data[off] << 8) |      /* first byte = high part (big-endian) */
                           (uint32_t)pkt_data[off + 1];
    break;
}
```
`LDPKT_B` and `LDPKT_W` are the same pattern with 1 and 4 bytes.

### 2.5 The DDoS attack packet, traced through the code
| Step | `pc` | Case taken | Effect |
|---|---|---|---|
| 1 | 0 | `LDPKT_H` | r1 = bytes 12–13 = 0x0800 |
| 2 | 1 | `JNE_K` | 0x0800 == 0x0800, so no jump; next_pc = 2 |
| 3 | 2 | `LDPKT_B` | r2 = byte 23 = 17 |
| 4 | 3 | `JNE_K` | equal, no jump |
| 5 | 4 | `LDPKT_H` | r3 = bytes 36–37 = 1900 |
| 6 | 5 | `JNE_K` | equal, no jump |
| 7 | 6 | `RET` | imm = 0 → **DROP**; `insns_executed = 7` |

### 2.6 Statistics (optional output)
`vm_run_stats_t` (in `vm_interpreter.h`) has `insns_executed`, `aborted_oob` and `aborted_budget`.
Callers may pass `NULL` if they don't want them. Every exit goes through `vm_finish()` (line 24), so
`insns_executed` is filled in on every path, also on a fault (D11, fixed 24 Nov; part 2 §7).

### 2.7 What the interpreter does NOT check
It does **not** check that register numbers are below 8. It trusts the verifier for that (`docs/ISA.md`
§8, decision Q3). That's why programs can only be installed through `vm_slot_install()`, which
always verifies first (D9, fixed 24 Nov; part 2 §8).

---

## 3. `vm_verifier.c`: the inspector

`vm_verify(prog)` (line 70) returns `VM_VERIFY_OK` or the **first** error it finds. The checks, in
order, with the line in `vm_verifier.c`:

| Rule | Check | Line | Error |
|---|---|---|---|
| V1 | program, instruction pointer and length are not NULL/0 | 72 | `ERR_EMPTY` |
| V2 | at most `VM_MAX_INSNS` instructions (default 64) | 75 | `ERR_TOO_LONG` |
| V3 | `ctx` is `VM_CTX_ETH` or `VM_CTX_IPV4` | 78 | `ERR_BAD_CTX` |
| *for every instruction, top to bottom:* | | | |
| V4 | opcode is one of the 33 known ones (`opcode_is_known`, line 8) | 87 | `ERR_BAD_OPCODE` |
| V5 | `dst` and `src` are < 8 (`is_reg_ok`, line 3) | 90 | `ERR_BAD_REG` |
| V6 | fields the opcode doesn't use are 0 (`unused_fields_are_zero`, line 60) | 93 | `ERR_UNUSED_FIELD` |
| V7 | `RET` returns 0 or 1 | 96 | `ERR_BAD_RET` |
| V8 | jumps go strictly forward (`target > pc`) | 105 | `ERR_BACKWARD_JUMP` |
| V9 | jumps land inside the program (`target < n`) | 112 | `ERR_BAD_JUMP` |
| *after the loop:* | | | |
| V10 | the last instruction is `RET` | 121 | `ERR_NO_RET` |

**Helper `used_fields()` (line 46)** says which fields each family uses. For example, `JMP` uses only
`offset`, and `RET` uses only `imm`. V6 checks that everything else is 0.

### Why these rules make the VM safe
- **It always stops (V8 + V9 + V10).** Every jump goes forward, so `pc` only ever increases. Each line
  runs at most once, so at most 512 steps. Jumps stay inside the program, and the last line is a
  return, so it can't run off the end.
- **It can't damage memory.** There is **no store instruction** at all. A program can only change its
  own 8 registers, and V5 guarantees those indexes are 0–7.
- **It can't read past the packet.** Every load is bounds-checked by the interpreter at run time (the
  packet length is only known then).
- **It fails safe.** Every problem → DROP.

---

## 4. `ddos_bytecode.h`: the rulebook

Two programs:
- **`ddos_program_eth`** (8 instructions, `ctx = VM_CTX_ETH`) reads bytes **12** (EtherType), **23** (IP
  protocol) and **36** (UDP destination port).
- **`ddos_program_ip`** (6 instructions, `ctx = VM_CTX_IPV4`) reads bytes **9** and **22**. lwIP has
  already removed the 14-byte Ethernet header, and it only sees IPv4, so there's no EtherType check.

Each line is `{ opcode, dst, src, offset, imm }`. All jumps go to the final `RET 1` (PASS); only the
full match reaches `RET 0` (DROP). The byte positions and the known limitations (IP options, VLAN,
fragments) are explained in `docs/ISA.md` and in our chat notes.

---

## 5. `tests/host/test_vm.c`: the proof that it works

**Run it:**
```bash
make -C tests/host
```
```bash
make -C tests/host asan
```
Expected final line for both: `190 passed, 0 failed` (125 on 28 Oct; part 2 explains the new ones). The second build adds AddressSanitizer and
UndefinedBehaviourSanitizer, which stop the run on any memory error or undefined behaviour.

**How it's built:**
- `CHECK(condition, message)` (`test_common.h` line 13) counts a pass, or prints `FAIL:` with the line number.
- `build_frame()` (`test_common.h` line 37) writes a fake Ethernet + IPv4 + UDP frame into a buffer, so tests can make
  attack and normal packets.
- Helpers `run_verified()` (line 271) and `r1_is()` (line 285) verify a small program, run it, and
  check the value left in register r1.

**Test groups (each prints a header line):**

| Function (line) | What it checks |
|---|---|
| `test_isa_layout` (37) | instruction is 8 bytes |
| `test_verifier` (43) | basic accept/reject cases |
| `test_ddos_filter` (99) | attack dropped; DNS, ARP, IPv6, TCP passed; short frames dropped |
| `test_known_limitations` (144) | IP options and VLAN currently pass (documents the limitation) |
| `test_alu_and_branches` (162) | a few instruction combinations |
| `test_isa_v1_encoding` (298) | opcode hex values, byte layout, all 256 opcode values, reserved class 7 |
| `test_isa_v1_verifier_rules` (348) | every rule V1–V10 at its boundary, and the error order |
| `test_isa_v1_unused_fields` (433) | rule V6 for every opcode and every field |
| `test_isa_v1_alu` (505), `_branches` (536) | every arithmetic and jump instruction against a small C "reference model" (`ref_alu`, `ref_jmp`) on 13 tricky value pairs |
| `test_isa_v1_loads` (576) | byte order, base register, wrap-around, every bounds edge |
| `test_isa_v1_moves_and_ret` (619) | registers start at 0 (all eight), moves, `RET` values |
| `test_runtime_guards_unverified` (676) | interpreter safety nets on deliberately bad programs |
| `test_insns_executed_all_paths` (700), `test_vm_slot` (749) | part 2: D11 and D9 |

**Mutation check (done on 28 Oct):** we broke the interpreter on purpose four ways to see whether the
tests notice. Three were caught directly. The fourth (a shift bug that is undefined behaviour) only
showed up under the sanitizers, so we made sanitizer findings fail the run.

---

## 6. Exercises (answers below)

1. What does `{ VM_OP_LDPKT_B, 4, 0, 14, 0 }` do?
2. Will the verifier accept `{ VM_OP_JEQ_K, 1, 0, -1, 0 }`? Which rule decides?
3. What is the opcode number of `LDPKT_W`, and which class is it in?
4. A 30-byte IPv4/UDP frame enters `ddos_program_eth`. What is the verdict, and why?
5. The DDoS program uses `[r0+12]`. Why is r0 always 0 there?
6. Why must every program end with `RET`?

<details><summary>Answers</summary>

1. It reads 1 byte at position 0 + 14 into r4: the IPv4 version/header-length byte (e.g. 0x45).
2. No. The target is 0 + 1 − 1 = 0, a jump to itself (an endless loop). Rule **V8**, `ERR_BACKWARD_JUMP`.
3. `(2 << 3) | 0 = 0x10`, class 0 (loads).
4. **DROP.** Bytes 12–13 and 23 are inside 30 bytes, so the first two checks pass (IPv4, UDP). Reading 2
   bytes at 36 would go past the end (36 > 30 − 2), so the bounds check fails → fail-safe DROP.
5. All registers start at 0, and the program never writes r0. It's a convention, not a hardware rule.
6. So execution can never run off the end of the array (rule **V10**). Together with forward-only jumps,
   this guarantees every run ends at a `RET`.

</details>

---
---

# Part 2: the slot, the hooks, the native filter, the configs, the emulator (24 Nov)

**Read in this order** (all short files):
1. `firmware/VM_Engine/vm_slot.h` / `.c`: the "gatekeeper" (only verified programs get in).
2. `firmware/Network_Hooks/hook_common.h`: what both hooks share.
3. `firmware/Network_Hooks/xdp_dma_hook.h` / `.c`: the early hook (driver layer).
4. `firmware/Network_Hooks/lwip_ip4_hook.h` / `.c` / `_glue.c`: the late hook (inside lwIP).
5. `firmware/DDoS_Prototype/ddos_native.h` / `.c`: the same filter written in plain C.
6. `firmware/DDoS_Prototype/exp_config.h`, `ddos_prototype_init.c`: the five experiment configs.
7. `sim/qemu_m33/`: running all of it on an emulated Cortex-M33 and counting instructions.

What changed today, as commits (`git log --oneline`):

| Commit | Prefix | What |
|---|---|---|
| `24f1436` | role1 | D11: instruction counter filled on every exit |
| `3d23d93` | role1 | D9: `vm_slot` install API |
| `cf60da0` | role2 | both hooks rewritten as pure functions (fixes D2–D4, D6–D8) |
| `980d663` | role2 | `tools/xc_check.sh`: Cortex-M33 compile check, including lwIP's `ip4.c` with our hook |
| `aebc439` | role2 | native C filter + differential test |
| `3f3a1af` | joint | `EXP_CONFIG` C0–C4 |
| `96f85bd` | role2 | QEMU harness |
| `fa1f628` | joint | first instruction counts saved in `eval/raw/` |
| `c8c8139` | role1 | interpreter: no more `memset` (−39% instructions) |
| `de6e247` | joint | instruction counts after the fix |

---

## 7. D11: the instruction counter on every exit (`vm_interpreter.c` line 24)

Before, `insns_executed` was only written when the program reached `RET`. If a load faulted (short
packet), the statistics said "0 instructions", which is wrong and would spoil measurements.

Now every `return` in the interpreter goes through one small helper:
```c
static inline int vm_finish(vm_run_stats_t *out, vm_run_stats_t *st, uint32_t executed, int verdict)
{
    st->insns_executed = executed;   /* always filled in */
    if (out) *out = *st;             /* copy out only if the caller asked */
    return verdict;
}
```
**Rule:** `insns_executed` = instructions *started*, including the one that faulted. A 20-byte IPv4
frame runs instructions 0 and 1, and instruction 2 (read byte 23) faults → 3.

**Tests** (`test_insns_executed_all_paths`, line 700): DROP path 7, ARP path 3, fault on the 3rd
instruction 3, fault on the 1st 1, NULL packet 0, budget stop exactly 2000. Four of these fail on the
old code. We checked that, so the tests really test the fix.

## 8. D9: `vm_slot`, the gatekeeper (`vm_slot.h`, `vm_slot.c`)

**The problem.** The interpreter trusts the verifier: it does not check register numbers (decision
Q3). So running an *unverified* program could read or write past the 8 registers on the stack. Before
today, `xdp_dma_hook_set_program(prog)` accepted any pointer. Nothing *enforced* "verify first".

**The fix.** A *slot* is a small struct that a hook owns:
```c
typedef struct {
    const vm_program_t *volatile prog;  /* NULL = nothing installed       */
    uint8_t ctx;                        /* what this hook delivers (ETH or IPV4) */
} vm_slot_t;
```
`vm_slot_install(slot, prog, &why)` (`vm_slot.c` line 12) does three things, in order:
1. `vm_verify(prog)`; if it fails → `VM_SLOT_ERR_VERIFY`, and `why` says which rule.
2. `prog->ctx == slot->ctx`? If not → `VM_SLOT_ERR_CTX`. (The IPv4 program reads byte 9 as "protocol";
   on an Ethernet frame byte 9 is part of a MAC address. Wrong hook = wrong answers.)
3. Only then: `slot->prog = prog`.

On **any** refusal the old program stays. A bad update never leaves a hook half-configured.

**Why `volatile`, and why is the swap safe?**
- `volatile` tells the compiler: "another task may change this, always really read it".
- A pointer is 32 bits, and on the Cortex-M33 an aligned 32-bit store is **atomic**: a reader sees the
  old or the new pointer, never half of each.
- The hook reads the pointer **once per packet** (`vm_slot_get`) into a local variable. So each packet
  is judged by one whole program.

**Lifetime rule:** an installed program must not change. Ours are `static const` (they live in flash
on the STM32). If you changed the bytes after verification, the verification would mean nothing.

**Tests** (`test_vm_slot`, line 749, 13 checks): right program accepted, wrong-hook program refused,
bad register refused with the reason, `ctx = 0` refused, NULL refused, old program kept after each
refusal, clear.

## 9. The hooks

### 9.1 What changed and why
The old driver hook (v0) tried to **replace** ST's Ethernet driver callbacks and buffer pool. On ST's
real code that can't work: it didn't compile (D2), would clash with ST's own callbacks (D6), and had
buffer bugs (D3, D4). The new design is much simpler: **ST's driver stays in charge; we offer one
function it calls.**

```
ST's driver (ethernetif.c, unchanged except one call)        our code (pure C)
HAL_ETH_RxLinkCallback(..., buff, Length)
    first segment of a frame?  ──── xdp_hook_run(buff, Length) ──►  PASS or DROP
    DROP → pbuf_free(p); return      (buffer back to ST's RX_POOL)
    PASS → link the buffer as before → lwIP
```
The exact 4 lines to add to ST's file are in the comment at the top of `xdp_dma_hook.h`.

**"Pure function"** = no HAL, no lwIP, no FreeRTOS inside. That gives three benefits:
- it can't conflict with ST's code;
- the same file runs on the Mac (tests), in QEMU and on the STM32;
- it is easy to explain and to measure.

### 9.2 `hook_common.h`: shared by both hooks
A `hook_t` holds a `vm_slot_t` (bytecode), a `native` function pointer (plain C filter) and counters.
**At most one filter is active**: installing bytecode removes the native filter, and vice versa.

`hook_run()` (the per-packet path, about line 93), in plain words:
```
prog   = slot program (read once)      native = native function (read once)
rx_total++
if prog:            verdict = vm_interpreter_run(prog, pkt, len)
elif native:        verdict = native(pkt, len)
else:               rx_no_filter_pass++; return PASS
if verdict == PASS: rx_passed++; return PASS
else:               rx_dropped++; return DROP       ← anything that isn't PASS counts as DROP (fail safe)
```
Counters always add up: `rx_total = rx_passed + rx_dropped + rx_no_filter_pass`. The old counter
`rx_dropped_no_prog` counted frames that actually *passed*, so it was renamed (D8).

### 9.3 Early hook: `xdp_dma_hook.c`
It is a thin wrapper: one `static hook_t s_hook` with `ctx = VM_CTX_ETH`. Its functions are
`xdp_hook_init`, `xdp_hook_install_vm`, `xdp_hook_install_native`, `xdp_hook_clear`, `xdp_hook_run`,
`xdp_hook_get_stats` and `xdp_hook_filter_kind`.

**Where it runs on the real board:** inside `HAL_ETH_RxLinkCallback()`. `HAL_ETH_ReadData()` calls that
from the **RX task** (`ethernetif_input`, highest priority), **not** from the interrupt. The interrupt
only gives the semaphore (see part 1, the RTOS discussion). Two driver facts, from reading ST's code,
are noted in the header:
- **D5:** after a DROP, `HAL_ETH_ReadData()` returns "OK, but no frame". ST's loop then stops reading,
  so the next frames wait for the next interrupt. Day 2 simulates both loops.
- **D14:** frames longer than 1000 bytes use two buffers. The hook sees only the first 1000, which is
  enough for our headers.

### 9.4 Late hook: `lwip_ip4_hook.c` + `lwip_ip4_hook_glue.c`
- **The filter core** (`lwip_ip4_hook.c`) is the same thin wrapper with `ctx = VM_CTX_IPV4`. `pkt[0]`
  is the IPv4 header.
- **The glue** (`_glue.c`) is the only file that includes lwIP. lwIP calls `lwip_ip4_hook_input(p, inp)`
  at the start of `ip4_input()`. We run the core on `p->payload`, `p->len`:
  - DROP: we must `pbuf_free(p)` ourselves and return 1 (lwIP's rule: "if you consume it, you free it").
  - PASS: return 0, and lwIP continues normally.
- **Registration** is two lines in `lwipopts.h`: `LWIP_HOOK_FILENAME` and `LWIP_HOOK_IP4_INPUT`.
  `tools/xc_check.sh` compiles lwIP's real `ip4.c` with those two lines and confirms it calls our function.
- Why split into core and glue? So the tests on the Mac can test the filter logic without lwIP.

### 9.5 Tests (`tests/host/test_hooks.c`, 18 checks)
These cover:
- no filter → pass;
- the wrong-context program is refused by each hook;
- the attack is dropped and DNS passes;
- a truncated frame is dropped;
- the counters add up;
- the native filter replaces the VM and vice versa;
- a refused install leaves the current filter in place;
- a filter that returns 7 counts as DROP;
- the two hooks are independent.

## 10. The native C filter and the differential test

### 10.1 `ddos_native.c`: same decisions, no interpreter
```c
int ddos_native_eth(const uint8_t *pkt, uint32_t len)
{
    if (pkt == 0 || len < 14u)      return VM_VERDICT_DROP;   /* can't read EtherType */
    if (be16(pkt + 12) != 0x0800u)  return VM_VERDICT_PASS;   /* not IPv4             */
    if (len < 24u)                  return VM_VERDICT_DROP;   /* can't read protocol  */
    if (pkt[23] != 17u)             return VM_VERDICT_PASS;   /* not UDP              */
    if (len < 38u)                  return VM_VERDICT_DROP;   /* can't read dst port  */
    return (be16(pkt + 36) == DDOS_TARGET_PORT) ? VM_VERDICT_DROP : VM_VERDICT_PASS;
}
```
**Why are the length checks spread out instead of one `if (len < 38) DROP` at the top?** Because
the bytecode checks the length **lazily**: each load checks only its own bytes. So a 20-byte **ARP**
frame passes in the VM: it reads bytes 12–13, sees "not IPv4" and passes without ever reading byte 36.
A single check at the top would drop it. Then native and VM would be two *different* filters, and
comparing their speed would be unfair. Each check here sits exactly where the VM would fault.

### 10.2 The differential test (`tests/host/test_native.c`)
"Differential" means running two implementations on the same inputs and demanding the same output.
- **Inputs** (fixed random seed, so every run checks exactly the same frames):
  - every length 0–64 of 6 hand-picked frames (every possible truncation point);
  - 20,000 random but well-formed frames;
  - 20,000 frames of pure random bytes.
- **Each frame is checked twice:** with the Ethernet variant on the whole frame, and with the IPv4
  variant on the frame minus 14 bytes.
- **Result:** 75,065 verdict pairs, 19,981 DROP and 55,084 PASS, **0 mismatches**.
- **Does the test catch mistakes?** We planted 3 one-character bugs in the native filter (one wrong
  length, another wrong length, a wrong protocol number). The test caught every one: 90, 61 and 5,522
  mismatches.

## 11. The experiment configurations (`exp_config.h`)

| Config | Filter | Where | Compares |
|---|---|---|---|
| C0 | none | — | baseline |
| C1 | native C | lwIP hook (late) | |
| C2 | VM | lwIP hook (late) | |
| C3 | native C | driver hook (early) | C1 vs C3 = effect of **placement** |
| C4 | VM | driver hook (early) | C3 vs C4 = cost of **interpretation**; C4 = our proposed system |

C5 (the MAC's hardware filter) and C6 (VM inside the interrupt) need the real board, so they are
refused (`#error`).
- **Build time:** `-DEXP_CONFIG=4` (the default is 4).
- **Run time:** `ddos_prototype_init_config(cfg)`, so one emulator image can run all five.

The test `test_exp_configs` (22 checks) confirms that, in each config, exactly the expected hook has
the expected kind of filter, the attack is dropped at that hook, and DNS passes everywhere.

## 12. The emulator: `sim/qemu_m33/`

### 12.1 What it is
**QEMU** is a program that pretends to be a computer. Its `mps2-an505` machine is an Arm development
board with a **Cortex-M33**, the same CPU core as our STM32H563. Our C code is compiled with the real
Arm compiler for that CPU and runs instruction by instruction in QEMU.

| Same as the STM32 | Different from the STM32 |
|---|---|
| CPU core (Cortex-M33, Thumb-2, FPU) | No STM32 peripherals (no Ethernet MAC, no ST HAL) |
| Compiler, flags, our C code | Memory map (QEMU board: RAM at 0x10000000) |
| Instructions executed | **Timing:** QEMU doesn't model cycles, caches or flash wait states |

So the right claim is: "our code runs correctly on a Cortex-M33 and executes N instructions". The
wrong claim would be "it takes X µs on the board".

### 12.2 The files
- `an505.ld`, the **linker script**: where things go in memory (all in the 4 MB RAM at 0x10000000;
  the vector table first, because the CPU looks there at reset).
- `startup.c`, what runs at power-on, before `main()`:
  1. enable the FPU;
  2. copy initial values (`.data`) and clear zero-initialised variables (`.bss`);
  3. set up printing;
  4. call `main()`;
  5. end QEMU with `main`'s return value.

  It also has a fault handler that prints the fault registers and the crashing address, then exits with
  code 3.
- **Semihosting:** the program asks QEMU to print and to exit, through a special `BKPT 0xAB`
  instruction. That's how `printf` and the exit code reach the Mac.
- `Makefile`: `make test`, `make bench` and `make record`.

### 12.3 `make -C sim/qemu_m33 test`: the whole test suite on the M33
These are the same 190 checks as on the Mac, compiled for the Cortex-M33: **190 passed**, QEMU exit
code 0. We also checked that a failing program gives exit code 1 and a crash gives exit code 3 with
the right address.

### 12.4 `make -C sim/qemu_m33 bench`: counting instructions
- **How:** QEMU can write a log line for every block of code it runs. With "one instruction per
  block", that is **one line per executed instruction**.
- **What is counted:** in `bench_insns.c`, the call to measure sits between `sim_mark_begin()` and
  `sim_mark_end()`. `count_insns.py` counts the log lines between those two markers.
- **Self-check:**
  - `calib_empty` (nothing between the markers) measures the markers' own cost, which is subtracted.
  - `calib_nop10` (exactly 10 NOP instructions) **must** measure 10, or the script refuses to output
    anything. It measures 10.
- **Deterministic:** a second run gives a byte-identical CSV.

**Results** (`-O2`, `eval/raw/2026-11-24_c8c8139_qemu_insns-O2/bench.csv`):

| Path (Ethernet variant) | VM bytecode instructions run | Cortex-M33 instructions: VM | Native C |
|---|---|---|---|
| attack (UDP to 1900) → DROP | 7 | 216 | 23 |
| other UDP → PASS | 7 | 216 | 23 |
| TCP → PASS | 5 | 158 | 17 |
| ARP → PASS | 3 | 104 | 12 |
| 20-byte runt → DROP (fault) | 3 | 110 | 14 |

| Whole hook call, attack frame | C0 | C1 native@lwIP | C2 VM@lwIP | C3 native@driver | C4 VM@driver |
|---|---|---|---|---|---|
| Cortex-M33 instructions | 19 | 36 | 178 | 40 | 235 |

How to read this:
- **Interpretation costs about 28 machine instructions per bytecode instruction**, plus about 20
  fixed: (216 − 104) / (7 − 3) = 28. The VM is about **9× more instructions than native C** for
  this small filter. That's the honest price of programmability, a negative result the thesis must
  report.
- **Absolute cost is still small.** 235 instructions is at least 235 cycles: at 250 MHz, **at least
  0.94 µs**. For scale: 100 Mbit/s of minimum-size frames is 148,800 frames/s, about 1,680 cycles per
  frame at 250 MHz. Real cycles are higher than instruction counts (loads and branches take extra
  cycles; flash has wait states), so these are **lower bounds**.
- **C2 vs C4 here is not the placement result.** The two hook calls cost about the same. Early
  filtering saves the work lwIP would do *before* the late hook: the mailbox hand-off to tcpip_thread,
  a context switch and `ethernet_input()`. That needs real lwIP, which is Day 2.
- `hook_C1_arp` / `hook_C2_arp` in the CSV are not meaningful: ARP never reaches `ip4_input()`.

### 12.5 The `memset` finding (an interpreter optimisation)
The first run showed the 3-instruction ARP path costing **240** instructions. The trace showed 133 of
them inside `memset`. The line `vm_regs_t regs = {{0}};` (zero the 8 registers) had been compiled into
a call to the C library's `memset`. The small embedded C library (newlib-nano, which ST's projects also
use) does that byte by byte. Writing the 8 zeros explicitly fixed it:

| `-O2` | before | after |
|---|---|---|
| VM, attack path | 352 | 216 (−39%) |
| VM, ARP path | 240 | 104 (−57%) |

The behaviour is unchanged (all registers still start at 0). A new test makes sure that r7 really
starts at 0 even after a run that left junk in it. It fails if one of the 8 stores is forgotten (we
checked). Both runs are saved in `eval/raw/` (`96f85bd` = before, `c8c8139` = after), so the
improvement is reproducible. This is real, measured evidence for Kiran's resume bullet "optimized the
interpreter for low CPU cycles".

## 13. Run it yourselves
```bash
make -C tests/host
```
```bash
make -C sim/qemu_m33 test
```
```bash
make -C sim/qemu_m33 bench
```
```bash
sh tools/xc_check.sh
```
Expected:
- the first two end with `190 passed, 0 failed`;
- the third prints the CSV table above;
- the fourth prints code sizes. Cortex-M33 at `-O2`: interpreter 1064 B, verifier 765 B, slot 273 B,
  native filter 88 B.

## 14. Exercises, part 2 (answers below)
7. `xdp_hook_install_vm(&ddos_program_ip, &why)`: what is returned, and what happens to the program
   that was installed before?
8. Why must the native filter not start with `if (len < 38) return DROP;`?
9. In C3, which counters change when an ARP frame arrives at the driver hook?
10. Why can't the emulator tell us the latency in microseconds?
11. The VM attack path is 216 instructions and the ARP path 104. How many instructions does one
    bytecode instruction cost on average?
12. Which configuration pair isolates the cost of interpretation, and which isolates placement?

<details><summary>Answers</summary>

7. `VM_SLOT_ERR_CTX`: the IPv4 program was written for the lwIP hook. The previous program stays
   installed.
8. A short non-IPv4 frame (e.g. a 20-byte ARP) would be dropped by native C but passed by the VM, so the
   two would be different filters and the comparison unfair (§10.1).
9. `rx_total` and `rx_passed` (the native filter runs and says PASS).
10. QEMU executes instructions correctly but doesn't model time: cycles per instruction, flash wait
    states, bus contention or interrupts. Instruction counts are a lower bound on cycles.
11. (216 − 104) / (7 − 3) = 28 machine instructions per bytecode instruction.
12. Interpretation: C3 vs C4 (same place, native vs VM). Placement: C1 vs C3 (same native filter,
    late vs early).

</details>
