# VM instruction set: ISA v1

**Status: frozen on 28 Oct 2026** (`VM_ISA_VERSION 1` in `vm_isa.h`). ISA v1 is the VM_Engine v0.1
behaviour plus the decisions in §11. A change to the encoding or semantics is a **new ISA version**:
bump `VM_ISA_VERSION` and change this document, the code and the host tests in the same commit.
`tests/host/test_vm.c` checks every rule here (the `[ISA v1]` sections).

Source of truth: `vm_isa.h` (encoding, opcodes, limits), `vm_verifier.c` (static rules),
`vm_interpreter.c` (run-time semantics). If this document and the code disagree, that is a bug: fix
one of them and record it in `docs/KNOWN_ISSUES.md`.

## 1. Purpose and design goals

A small, eBPF-inspired bytecode that inspects one received packet and returns a verdict
(PASS or DROP). It is designed for a Cortex-M33 MCU running FreeRTOS:

- **Safe by construction:** a program that passes the verifier always terminates, can only read
  the packet it was given, and cannot write any memory except its own 8 registers.
- **Fail safe:** every run-time fault ends in DROP, never PASS.
- **Small and deterministic:** no heap, no stores, bounded execution, fixed-size interpreter frame.

## 2. Machine model

| Item | Definition |
|---|---|
| Registers | 8 general-purpose 32-bit registers `r0`–`r7`, all set to 0 when a program starts. |
| Packet | Read-only byte array `pkt[0 .. len-1]`, passed in by the hook (zero-copy). `len` is the length the hook passes. |
| Memory | None. There is no VM stack, no scratch memory and no store instruction. |
| Program counter | Instruction index `pc`, starts at 0. |
| Result | A verdict: `PASS` (= 1) or `DROP` (= 0). |

`r0` is **not** hard-wired to zero. The bytecode in `ddos_bytecode.h` uses `r0` as the zero base for
packet loads by **convention**: it never writes `r0`. Nothing enforces this.

## 3. Instruction encoding

Every instruction is 8 bytes (`vm_insn_t`, checked by `_Static_assert`). Programs are arrays of
instructions. Byte layout in memory:

| Byte | Field | Meaning |
|---|---|---|
| 0 | `opcode` | Operation (see §4). |
| 1, bits 0–3 | `dst` | Destination register / register being compared (0–15 encodable, must be < 8). |
| 1, bits 4–7 | `src` | Source register / load base register (0–15 encodable, must be < 8). |
| 2–3 | `offset` | Signed 16-bit, little-endian. Branch distance, or packet offset for loads. |
| 4–7 | `imm` | Signed 32-bit, little-endian. Immediate operand or verdict. |

Example (checked on 28 Oct 2026 with Apple clang on the Mac and with arm-none-eabi-gcc 15.2.1 for
Cortex-M33; both give the same bytes): `{JNE_K, dst=3, src=5, offset=-2, imm=0x12345678}` is stored
as `13 53 FE FF 78 56 34 12`.

Note: the comment diagram in `vm_isa.h` draws the fields MSB-first. The table above is the actual
byte order. The assembler (`tools/asm/`, planned) must follow this table, not the diagram.

**Opcode structure:** `opcode = (operation << 3) | class`. The class is the low 3 bits:

| Class | Value | Meaning |
|---|---|---|
| `LD` | 0 | packet load |
| `ALU_K` | 1 | arithmetic/logic, operand = `imm` |
| `ALU_X` | 2 | arithmetic/logic, operand = register `src` |
| `JMP_K` | 3 | branch, compare with `imm` |
| `JMP_X` | 4 | branch, compare with register `src` |
| `RET` | 5 | return a verdict |
| `MISC` | 6 | moves |
| `RESERVED` | 7 | reserved for future versions (Q4); every opcode in this class is rejected |

## 4. Opcode table (33 opcodes)

Notation: `R[x]` is register `x`. `IMM` is `imm` sign-extended to 32 bits and then read as an unsigned
32-bit value (two's complement). All arithmetic is unsigned 32-bit and wraps modulo 2³².

| Hex | Name | Effect |
|---|---|---|
| 0x00 | `LDPKT_B` | `R[dst] = pkt[a]` (1 byte, zero-extended) |
| 0x08 | `LDPKT_H` | `R[dst] = pkt[a]<<8 \| pkt[a+1]` (2 bytes, big-endian) |
| 0x10 | `LDPKT_W` | `R[dst] = pkt[a]<<24 \| pkt[a+1]<<16 \| pkt[a+2]<<8 \| pkt[a+3]` (4 bytes, big-endian) |
| 0x01 / 0x02 | `ADD_K` / `ADD_X` | `R[dst] = R[dst] + IMM` / `+ R[src]` |
| 0x09 / 0x0A | `SUB_K` / `SUB_X` | `R[dst] = R[dst] - IMM` / `- R[src]` |
| 0x11 / 0x12 | `AND_K` / `AND_X` | `R[dst] = R[dst] & IMM` / `& R[src]` |
| 0x19 / 0x1A | `OR_K` / `OR_X` | `R[dst] = R[dst] \| IMM` / `\| R[src]` |
| 0x21 / 0x22 | `XOR_K` / `XOR_X` | `R[dst] = R[dst] ^ IMM` / `^ R[src]` |
| 0x29 / 0x2A | `LSH_K` / `LSH_X` | `R[dst] = R[dst] << (IMM & 31)` / `<< (R[src] & 31)` |
| 0x31 / 0x32 | `RSH_K` / `RSH_X` | `R[dst] = R[dst] >> (IMM & 31)` / `>> (R[src] & 31)` (logical shift) |
| 0x03 | `JMP` | `pc = pc + 1 + offset` (always) |
| 0x0B / 0x0C | `JEQ_K` / `JEQ_X` | if `R[dst] == IMM` / `== R[src]`: `pc = pc + 1 + offset` |
| 0x13 / 0x14 | `JNE_K` / `JNE_X` | if `!=`: jump |
| 0x1B / 0x1C | `JGT_K` / `JGT_X` | if `>` (unsigned): jump |
| 0x23 / 0x24 | `JGE_K` / `JGE_X` | if `>=` (unsigned): jump |
| 0x2B / 0x2C | `JLT_K` / `JLT_X` | if `<` (unsigned): jump |
| 0x33 / 0x34 | `JLE_K` / `JLE_X` | if `<=` (unsigned): jump |
| 0x06 | `MOV_K` | `R[dst] = IMM` |
| 0x0E | `MOV_X` | `R[dst] = R[src]` |
| 0x05 | `RET` | stop; verdict = `imm`, which must be 0 (DROP) or 1 (PASS) |

There is no `JMP_X` with operation 0 (0x04 is not an opcode). The verifier accepts exactly these 33
opcodes and rejects the other 223 (checked exhaustively by a host test).

## 5. Semantics in detail

**Packet loads.** The address is `a = R[src] + offset`, computed in unsigned 32-bit arithmetic
(`offset` is sign-extended, so a negative offset or a large base wraps around). The load is allowed
only if `size <= len` and `a <= len - size`. Otherwise the program stops with **DROP** (counted in
`aborted_oob`). Multi-byte loads are big-endian (network byte order), so header fields need no
byte swapping.

**ALU.** `imm` is sign-extended: `ADD_K r1, -1` subtracts 1. Shift counts use only the low 5 bits,
so a shift by 32 is a shift by 0. There is no division or multiplication, so ALU instructions cannot
fault.

**Branches.** The target is `pc + 1 + offset`. Comparisons are **unsigned**: `JGT_K r1, -1` compares
with `0xFFFFFFFF` and is never true. `JMP` ignores `dst`, `src` and `imm`.

**RET.** Ends the program with verdict `imm`. The verifier accepts only 0 (DROP) and 1 (PASS)
(rule V7, Q2). If an unverified program returns any other value, the interpreter still gives DROP
(defence in depth).

**Unused fields must be zero (canonical encoding, Q1).** Each opcode uses only some fields; every
other field must be 0, so each program has exactly one binary form:

| Opcodes | Uses | Must be 0 |
|---|---|---|
| `LDPKT_B/H/W` | `dst`, `src`, `offset` | `imm` |
| ALU `_K`, `MOV_K` | `dst`, `imm` | `src`, `offset` |
| ALU `_X`, `MOV_X` | `dst`, `src` | `offset`, `imm` |
| `JMP` | `offset` | `dst`, `src`, `imm` |
| `Jcc_K` | `dst`, `offset`, `imm` | `src` |
| `Jcc_X` | `dst`, `src`, `offset` | `imm` |
| `RET` | `imm` | `dst`, `src`, `offset` |

## 6. Programs and limits

A program is `vm_program_t { const vm_insn_t *insns; uint16_t n_insns; uint8_t ctx; }`. `ctx` is the
hook context the program was written for: `VM_CTX_ETH` (1) or `VM_CTX_IPV4` (2); see §9. 0 is invalid,
so a forgotten field is caught.

| Limit | Value | Where |
|---|---|---|
| Registers | 8 × 32 bit | `VM_NUM_REGS` |
| Program length | 1 to `VM_MAX_INSNS` (embedded default: 64 instructions / 512 B) | Configurable build-time bound, verifier |
| Run-time step budget | 2000 steps | `VM_MAX_STEPS`, interpreter |
| Instruction size | 8 bytes | `vm_insn_t` |

## 7. Verifier rules (`vm_verify()`)

Checked once, before a program may run. The verifier returns the **first** error it finds: first the
whole-program checks, then instructions in order `pc = 0 .. n-1` (per instruction: opcode, then
registers, then unused fields, then `RET` value, then branch target), and finally the last-instruction
check.

| Rule | Check | Error code |
|---|---|---|
| V1 | program pointer, `insns` and `n_insns` are non-zero | `VM_VERIFY_ERR_EMPTY` |
| V2 | `n_insns <= VM_MAX_INSNS` (default 64) | `VM_VERIFY_ERR_TOO_LONG` |
| V3 | `ctx` is `VM_CTX_ETH` or `VM_CTX_IPV4` (checked after V1 and V2) | `VM_VERIFY_ERR_BAD_CTX` |
| V4 | every opcode is one of the 33 in §4 | `VM_VERIFY_ERR_BAD_OPCODE` |
| V5 | every `dst` and `src` is `< 8` (also in fields the opcode doesn't use) | `VM_VERIFY_ERR_BAD_REG` |
| V6 | every field the opcode does not use is 0 (§5) | `VM_VERIFY_ERR_UNUSED_FIELD` |
| V7 | every `RET` has `imm` 0 or 1 | `VM_VERIFY_ERR_BAD_RET` |
| V8 | every branch target is **strictly forward**: `target > pc` (offset ≥ 0) | `VM_VERIFY_ERR_BACKWARD_JUMP` |
| V9 | every branch target is inside the program: `target < n_insns` | `VM_VERIFY_ERR_BAD_JUMP` |
| V10 | the last instruction is `RET` | `VM_VERIFY_ERR_NO_RET` |

What the verifier does **not** check: packet offsets against the frame length, because the length is
unknown until a packet arrives (see §8 and gap G3).

## 8. Run-time checks and guarantees

The interpreter (`vm_interpreter_run()`) checks, and returns DROP on failure:

| Check | When | Stats counter |
|---|---|---|
| program, `insns` or packet pointer is NULL, or `n_insns == 0` | entry | none |
| `pc >= n_insns` | every step | `aborted_oob` |
| more than 2000 steps | every step | `aborted_budget` |
| packet load out of bounds | every load | `aborted_oob` |
| unknown opcode | every step | `aborted_oob` |

**Termination (proof sketch).** For a verified program, every instruction moves `pc` strictly
forward: non-branches go to `pc + 1`, and branches go to a target `> pc` (V8). So each instruction
runs at most once, and a run takes at most `n_insns <= VM_MAX_INSNS` steps (64 by default). The last instruction is `RET`
(V10), and every branch target is inside the program (V9), so execution can't fall off the end. The
2000-step budget can therefore **never** trigger for a verified program. It is kept only as
defence in depth (KNOWN_ISSUES D12).

**Memory safety (argument).** The VM has no store instruction, so a program can only write its own
8 registers. It reads instruction memory only at `pc < n_insns` (checked every step), and packet
memory only after a bounds check. **Important:** register indices are checked **only by the
verifier (V5)**. The interpreter indexes its 8-entry register array without a run-time check, so
running an **unverified** program with `dst` or `src` ≥ 8 would read or write past the array on the
C stack. This is why every install path must call `vm_verify()` first (CLAUDE.md rule 2; KNOWN_ISSUES
D9). Decision Q3: the interpreter will **not** add a run-time register check, as in eBPF. Safety rests
on verify-on-install, and each instruction stays cheap.

**Interpreter resources.** No heap, no globals. The register file lives in the interpreter's C stack
frame. Dispatch is a `switch` that GCC compiles to a single `TBH` jump table on Cortex-M33 (re-checked
28 Oct 2026 with arm-none-eabi-gcc 15.2.1 at `-O2`). Code size and stack depth depend on the compiler
and flags: see `docs/LOG.md` for measured values. Thesis numbers must come from the firmware's own
compiler.

## 9. Hook calling convention

| Hook | What `pkt[0]` is | Required `ctx` | Program variant |
|---|---|---|---|
| Driver hook (`HAL_ETH_RxLinkCallback`, C3/C4) | first byte of the **Ethernet frame** (destination MAC) | `VM_CTX_ETH` | `ddos_program_eth` |
| lwIP hook (`ip4_input()`, C1/C2) | first byte of the **IPv4 header** | `VM_CTX_IPV4` | `ddos_program_ip` |

The two variants implement the same logic with offsets that differ by 14 bytes. Each declares its
`ctx` (Q5). The verifier checks that `ctx` is a known value. **Each hook's install function must also
refuse a program whose `ctx` doesn't match the hook.** That check arrives with the verify-on-install
API (KNOWN_ISSUES D9, plan step 3.2); until then, nothing enforces the match. In both hooks, `len` covers
only the first buffer segment (KNOWN_ISSUES D10/D14; ST's RX buffers are 1000 bytes).

## 10. Differences from eBPF and from the mid-term deck

| Topic | This ISA | Notes |
|---|---|---|
| Registers | 8 × 32-bit, no frame pointer | eBPF: 11 × 64-bit |
| Memory | none: no stack, no maps, no stores | deck slide 4 says "fixed 512-byte stack": **not implemented** (gap G2) |
| Loops | forbidden: forward-only branches | like classic BPF; eBPF now allows bounded loops |
| Packet bounds | checked **at run time** on every load | deck slide 5 implies a static check; static analysis is planned (gap G3, plan step 6.1) |
| Dispatch | `switch` → one `TBH` jump table | deck says "computed jump table"; a computed-goto version is planned for benchmarking (gap G8) |
| Comparisons | unsigned only | eBPF also has signed compares |

## 11. Design decisions for v1 (decided 28 Oct 2026)

All six recommendations were accepted.

| # | Question | Decision |
|---|---|---|
| Q1 | Should the verifier require **unused fields to be zero** (canonical encoding)? | **Decided 28 Oct 2026: yes.** Implemented as rule V6 (§5, §7). |
| Q2 | Should the verifier **reject `RET` with `imm` other than 0 or 1**? | **Decided 28 Oct 2026: yes.** Implemented as rule V7 (§5, §7). |
| Q3 | Should the interpreter also **mask register indices** (`& 7`) at run time? | **No.** Rely on verify-on-install (D9), as eBPF does, and keep the per-instruction cost down (§8, `vm_interpreter.h`). |
| Q4 | **Reserve class 7** for future extensions (the stateful counter map for rate limiting in Feb; an optional scratch stack for G2)? | **Yes.** `VM_CLASS_RESERVED` (7) is defined in `vm_isa.h`; the verifier rejects all 32 of its opcodes. Using it means ISA v2. |
| Q5 | Should a program **declare its hook context** (Ethernet frame vs IP packet), so installing it on the wrong hook is refused? | **Decided 28 Oct 2026: yes.** `vm_program_t.ctx` plus verifier rule V3 are implemented. The hook-match check comes with D9 (plan 3.2). |
| Q6 | Add **signed comparisons**? | **No.** Packet header fields are unsigned; all compares stay unsigned (§5). |
