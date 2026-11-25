# Project summary in simple terms

For Nishant and Kiran. Plain sentences, real technical terms, no code. Read this first (about
40 minutes). `WALKTHROUGH.md` has the line-by-line code explanation if a question needs it.

All numbers come from logged runs in `eval/raw/` (the run used here: `2026-11-24_3828f4c_sim`).
They are **instruction counts on an emulated Cortex-M33**, not hardware measurements.

---

## 1. The project in brief

- **Platform:** STM32H563 (Cortex-M33, 250 MHz) running FreeRTOS and the lwIP TCP/IP stack.
- **Problem:** in a DDoS flood, every junk packet goes through the Ethernet driver, the FreeRTOS
  mailbox and lwIP before it is discarded. That wastes CPU time.
- **Idea (from Linux XDP/eBPF):** filter packets in the **Ethernet driver's RX path**, before lwIP.
  The filter is a **bytecode program** run by a small **virtual machine (VM)**. It returns **PASS** or
  **DROP**.
- **Why bytecode and not plain C:**
  - a new filter is data, so it can be installed without rebuilding the firmware;
  - a **verifier** checks every program before it runs, so a bad program cannot hang or crash the
    device.
- **What we compare:**
  - the VM against the same filter written in **native C**;
  - the **driver hook** (early) against an **lwIP hook** in `ip4_input()` (late).

---

## 2. The VM (Kiran's part: `firmware/VM_Engine/`)

### 2.1 Instruction format (ISA v1)
- Every instruction is **8 bytes (64 bits)**:

  | Field | Size | Meaning |
  |---|---|---|
  | `opcode` | 8 bits | the operation |
  | `dst` | 4 bits | destination register |
  | `src` | 4 bits | source register |
  | `offset` | 16 bits | jump distance, or packet position for loads |
  | `imm` | 32 bits | constant |

- **33 opcodes** in 7 classes: packet load, ALU with a constant, ALU with a register, jump with a
  constant, jump with a register, return, move. Class 7 is reserved for ISA v2.
- `opcode = (operation << 3) | class`. For example, `JNE_K = (2 << 3) | 3 = 0x13`.
- **8 registers** `r0`–`r7`, each 32 bits, all 0 when a program starts.
- **Loads read big-endian** (network byte order): `LDPKT_B` reads 1 byte, `LDPKT_H` 2, `LDPKT_W` 4.
- **Jump target** = `pc + 1 + offset`. The `+1` is there because `pc` has already moved past the
  jump instruction.
- **`RET 0` = DROP, `RET 1` = PASS.**

### 2.2 The DDoS program (`ddos_bytecode.h`)
It drops UDP packets to port 1900 (the SSDP amplification port). There are two versions, because the
two hooks see the packet from different starting points.

| Driver-hook version (Ethernet frame) | What it checks | If not matched |
|---|---|---|
| `LDPKT_H r1, [r0+12]` + `JNE_K` | EtherType == 0x0800 (IPv4) | PASS |
| `LDPKT_B r2, [r0+23]` + `JNE_K` | IP protocol == 17 (UDP) | PASS |
| `LDPKT_H r3, [r0+36]` + `JNE_K` | UDP destination port == 1900 | PASS |
| `RET 0` | all three matched | DROP |

- 8 instructions. The DROP path runs 7 of them; a non-IPv4 frame runs 3.
- The lwIP-hook version reads offsets 9 and 22 instead. lwIP has already removed the 14-byte Ethernet
  header, and only IPv4 reaches `ip4_input()`.
- **Known limitations:**
  - the offsets are fixed, so it assumes IHL = 5 (no IP options);
  - VLAN tags are not parsed;
  - it is UDP only;
  - IP fragments can be misread.

### 2.3 The verifier (`vm_verifier.c`)
It runs **once, at install time**, and checks rules V1–V10:
- the program is not empty and has at most `VM_MAX_INSNS` instructions (64 by default);
- `ctx` is valid (which hook the program is for);
- every opcode is known and every register is < 8;
- fields an instruction doesn't use must be 0;
- `RET` returns only 0 or 1;
- **jumps go forward only** and land inside the program;
- the last instruction is `RET`.

**What these rules guarantee:**
- **Termination:** forward-only jumps mean each instruction runs at most once, so at most 512 steps.
- **Memory safety:** there is no store instruction; registers are range-checked by the verifier;
  every packet load is bounds-checked at run time (the packet length is only known then).
- **Fail-safe:** any fault returns DROP, never PASS.

### 2.4 The interpreter (`vm_interpreter.c`)
- A `switch` loop: fetch, decode, execute, next `pc`. There is no heap; the registers live on the
  stack.
- Each packet load checks bounds with an overflow-safe test (`pkt_bounds_ok`).
- It trusts the verifier for register numbers (decision Q3). That is why programs must go through
  `vm_slot_install()`.

---

## 3. The hooks (Nishant's part: `firmware/Network_Hooks/`)

### 3.1 Where a packet goes on the STM32
```
ETH DMA writes frame to RX buffer -> ETH IRQ -> HAL_ETH_RxCpltCallback (ISR): give RxPktSemaphore
-> RX task (prio 48) takes semaphore -> HAL_ETH_ReadData() -> HAL_ETH_RxLinkCallback()   <- DRIVER HOOK
-> tcpip_input(): message into lwIP mailbox -> tcpip_thread (prio 40) -> ethernet_input()
-> ip4_input()                                                                              <- lwIP HOOK
-> udp_input() -> application
```

### 3.2 Driver hook (`xdp_dma_hook.c`)
- `xdp_hook_run(frame, len)` is a pure function: no HAL, no lwIP, no FreeRTOS code inside it.
- ST's `ethernetif.c` keeps its RX callbacks and its `RX_POOL` buffers. We add 4 lines in
  `HAL_ETH_RxLinkCallback()`: on the first segment of a frame, call the hook; on DROP, `pbuf_free()`
  the buffer back to `RX_POOL` and return without linking it.
- It runs in the **RX task**, not in the ISR. The ISR only gives the semaphore.

### 3.3 lwIP hook (`lwip_ip4_hook.c`, `lwip_ip4_hook_glue.c`)
- It is registered in `lwipopts.h` with `LWIP_HOOK_FILENAME` and `LWIP_HOOK_IP4_INPUT`.
- On DROP, the hook must `pbuf_free()` the packet and return 1 (lwIP's rule).

### 3.4 Shared parts
- **`hook_common.h`:** each hook runs at most one filter (VM or native C), and keeps counters
  (total, passed, dropped, passed with no filter).
- **`vm_slot` (D9):** the only way to install a program. It runs the verifier, checks that `ctx`
  matches the hook, and keeps the old program if either check fails. The pointer is `volatile` and is
  read once per packet.

### 3.5 Native C filter (`ddos_native.c`)
- The same three checks, written in C.
- The length checks sit at the same points where the VM would fault. So both give the same verdict on
  every input, including short frames.
- **Differential test:** 75,065 frames, 0 mismatches. 3 planted bugs were all detected.

### 3.6 Experiment configs (`exp_config.h`)
| Config | Filter | Hook |
|---|---|---|
| C0 | none | — |
| C1 | native C | lwIP |
| C2 | VM | lwIP |
| C3 | native C | driver |
| C4 | VM | driver (**proposed system**) |

C1 vs C3 measures the effect of **placement**. C3 vs C4 measures the **interpretation overhead**.
C5 (the MAC hardware filter) and C6 (VM in the ISR) need the board.

---

## 4. How it is tested without a board

| Level | Tool | Shows |
|---|---|---|
| Unit tests | `make -C tests/host` (Mac, plus ASan/UBSan) | logic: 209 checks |
| Cross-compile | `sh tools/xc_check.sh` (arm-none-eabi-gcc 15.2.1) | compiles for Cortex-M33 against ST's lwIP headers; code size and stack |
| Emulation | QEMU `mps2-an505` (Cortex-M33) | the same code runs on the same CPU core; instruction counts |

**Instruction counting:**
- QEMU runs with one instruction per translation block plus an exec trace, so the trace has one line
  per executed instruction.
- `count_insns.py` counts the lines between `sim_mark_begin()` and `sim_mark_end()`.
- Calibration: 10 NOPs must measure exactly 10, or the script refuses to output anything.
- An instruction count is a **lower bound on cycles**: QEMU does not model flash wait states, pipeline
  stalls or bus contention.

---

## 5. The simulated system (Day 2: `sim/`)

### 5.1 Simulated ST Ethernet driver (`sim/eth_sim/`)
- A model of the RX DMA ring: **4 descriptors**, each owned by the DMA or by software. If no
  descriptor is free, the MAC drops the frame and counts it (like `ETH->DMACMFCR`).
- `HAL_ETH_ReadData()` and the descriptor refill copy **HAL 1.6.0** step by step:
  - one frame per call;
  - it returns `HAL_OK` with a NULL buffer when the link callback linked nothing (our DROP).
- ST's `ethernetif.c` RX side: `RX_POOL` of **12 × 1000-byte** buffers (custom pbufs), the three
  callbacks, `low_level_input()`, and both RX loops:
  - **ST's loop:** `do { p = low_level_input(); ... } while (p != NULL);` stops at the first dropped
    frame (**D5**);
  - **fixed loop:** keeps reading while `HAL_ETH_ReadData()` returns `HAL_OK`.

### 5.2 lwIP 2.2.1
- ST's version and ST's options (checksums by hardware, memory sizes). DHCP and TCP are off.
- UDP echo on port 7; ICMP replies (ping, port unreachable).

### 5.3 FreeRTOS 11.2.0 (`sim/rtos/`)
- Port `ARM_CM33_NTZ`. `FreeRTOSConfig.h` is copied from ST's example; each difference is marked
  `DIFF:`. One difference is `configENABLE_FPU 1`, the **D13 fix**.
- Tasks as in ST's example:
  - RX task `EthIf`, priority 48, 1024-byte stack;
  - `tcpip_thread`, priority 40, 2048 bytes;
  - a test-harness **feeder** task at priority 1, which writes frames into the ring and raises a
    **real NVIC interrupt**.
- The ISR calls `xSemaphoreGiveFromISR()`, exactly as ST's does.
- QEMU runs with `-icount`, so the 1 kHz SysTick is tied to instruction counts and every run is
  deterministic.
- Stack overflow check level 2 and `configASSERT` are on: no failures in 240 runs. Lowest free stack
  seen: RX task 792 / 1024 bytes, `tcpip_thread` 1608 / 2048 bytes.

### 5.4 Traffic (`bench/gen_traces.py`)
- Standard `.pcap` files (open them in Wireshark), with valid checksums and a fixed random seed.
- Traces:
  - `legit`: echo, ping, DNS;
  - `attack`: UDP to port 1900 from spoofed sources;
  - `mix50` / `mix90` / `mix99`;
  - `odd`: VLAN, IP options, fragments, IPv6, TCP;
  - `measure`;
  - `loader_demo`.
- **burst** = frames written to the ring before the RX task runs. burst 1 means the task keeps up;
  larger bursts mean heavier load.

### 5.5 Runtime rule update (gap G9)
- **XVM1 message:** magic `XVM1`, ISA version, `ctx`, instruction count, CRC-32, then the
  instructions.
- **`vm_loader`** checks the header and the CRC, copies the program into one of **two static buffers
  per hook**, and installs it through `vm_slot` (verifier + `ctx` check).
  - A load never overwrites the program that is installed.
  - Without this double buffering, a refused program overwrote the running one and the test run
    crashed.
- **`tools/asm/bcgen.py`:** an assembler from text (`ldh r1, [r0+12]`, `jne r1, 0x0800, +5`, ...) to
  an XVM1 message.
- The firmware receives messages on **UDP port 9000** and replies `OK` or `ERR <reason>`.
- **Demo (C4):** an invalid program (backward jump) is refused and the old filter stays. A program
  blocking port 53 is installed, and the verdicts change with no rebuild.
- **Limitation:** the sender is not authenticated.

---

## 6. Results

### 6.1 Correctness
- In every config, legit traffic is answered and the attack is dropped at the configured hook.
- Mac and QEMU builds give identical results.
- No-RTOS and FreeRTOS builds give identical results at burst 1 (60 of 60 runs).

### 6.2 Instructions per frame, FreeRTOS build (`fig1_per_frame_cost.png`)
This is the whole receive path: ISR, RX task, hook, mailbox, `tcpip_thread`, lwIP and the reply.

| Frame | C0 | C1 | C2 | C3 | **C4** |
|---|---|---|---|---|---|
| attack | 5014 | 2952 | 3094 | 1531 | **1726** |
| legit echo | 4888 | 4904 | 5046 | 4909 | 5104 |

- **Placement:** for the VM, the driver hook costs 44% less than the lwIP hook (C2 → C4); for native
  C, 48% less (C1 → C3). A late drop still pays for the mailbox post and the context switch to
  `tcpip_thread`.
- **Interpretation overhead:** about 195 instructions per frame (C3 → C4).
- **C4 vs no filter:** 66% fewer instructions per attack frame. A legit frame costs 4.4% more.

### 6.3 Effect of the RTOS (`fig2_rtos_vs_nosys_attack.png`)
Without the RTOS, the placement saving is 26% (C1 → C3: 462 → 344). With FreeRTOS it is 48%. So the
benefit of early filtering comes mainly from skipping the mailbox hand-off and the task switch.

### 6.4 Filter only, and the memset fix (`fig5_memset_fix.png`)
- VM filter call: **216** instructions; native C: **23** (about 9×).
- Cost: about 28 instructions per bytecode instruction, plus about 20 fixed.
- **Optimisation:** `regs = {{0}}` compiled to newlib-nano's byte-wise `memset`. Eight explicit stores
  cut the attack path from 352 to 216 (−39%).

### 6.5 D5 (`fig4_d5_rx_loop.png`)
In C4, with 90% attack traffic, the share of legit echoes answered is:

| Frames per RX-task wake-up | ST's RX loop | Fixed loop |
|---|---|---|
| 2 | 55% | 100% |
| 4 | 29% | 100% |

With ST's loop, frames stay stranded in the ring after a drop, the 4 descriptors fill up, and the MAC
drops new frames. At 8 frames per wake-up, both loops lose frames, because the ring has only 4
descriptors.

### 6.6 Flood model (`fig3_flood_model.png`; a MODEL, not a measurement)
- **Assumptions:** 250 MHz; cycles = CPI × instructions; 1000 legit frames/s; frames are lost in
  proportion when the CPU is saturated.
- **At 100 Mbit/s** of this attack (89,928 frames/s), legit traffic delivered:

  | | C0 | C4 |
  |---|---|---|
  | CPI 1 | 55% | 100% |

- **At CPI 2**, C4 saturates at about **69,500 frames/s**. CPI 1 is optimistic, so state the range.

### 6.7 Footprint (`fig6_footprint.png`, arm-none-eabi-gcc 15.2.1, -O2)
| Part | Code (.text) |
|---|---|
| interpreter | 1064 B |
| verifier | 765 B |
| slot | 273 B |
| loader | 797 B code in the recorded baseline; current default uses 2 × 512 B RAM buffers per hook |
| **VM engine total** | **2,899 B** |
| each hook | 212 B |
| native filter | 88 B |

Interpreter stack: 72 B.

### 6.8 New finding (D15)
Under FreeRTOS with bursts and no filter (C0), IP fragments held for reassembly plus frames queued in
the mailbox used **all 12 `RX_POOL` buffers**. C3/C4 never ran out, because junk is dropped before it
holds a buffer.

---

## 7. Claims
- **Say:** "Tested on an emulated Cortex-M33 (QEMU) with FreeRTOS 11.2.0, lwIP 2.2.1 and a model of
  ST's Ethernet driver. Numbers are instruction counts, a lower bound on cycles."
- **Don't say:**
  - "measured on the STM32";
  - any time in µs;
  - "handles 100 Mbit/s" (only the model at CPI 1 says that).
- **Not tested:**
  - real hardware and real timing;
  - the MAC hardware filter (C5) and the VM in the ISR (C6);
  - authenticated rule updates.

---

## 8. Terms

| Term | Meaning |
|---|---|
| XDP / eBPF | Linux framework for running verified bytecode in the network driver |
| ISA | the instruction set: format and meaning of each instruction |
| Verifier | static check of a program before it is installed |
| Interpreter | executes bytecode one instruction at a time |
| `ctx` | which hook a program is written for (`VM_CTX_ETH` / `VM_CTX_IPV4`) |
| Descriptor / RX ring | DMA bookkeeping: where the MAC writes each received frame |
| pbuf / RX_POOL | lwIP packet buffer / ST's pool of 12 zero-copy RX buffers |
| Mailbox | FreeRTOS queue through which the RX task hands packets to `tcpip_thread` |
| ISR | interrupt service routine; here it only gives the semaphore |
| CPI | cycles per instruction |
| QEMU `-icount` | QEMU's clock is tied to the number of executed instructions |
| Differential test | two implementations, same inputs, the outputs must match |
| pcap | standard packet-capture file format |

---

## 9. Likely questions
1. **Why a VM instead of native C?** A program can be replaced at run time as data, and the verifier
   proves it terminates and cannot write memory. The cost is about 195 extra instructions per frame
   in our measurement.
2. **How is termination guaranteed?** Forward-only jumps, at most `VM_MAX_INSNS` instructions (64 by default), and the last
   instruction must be `RET`.
3. **How is memory safety guaranteed?** There are no store instructions; register numbers are checked
   by the verifier; every packet load is bounds-checked at run time; any fault returns DROP.
4. **Where does the VM run: ISR or task?** In the RX task, inside `HAL_ETH_RxLinkCallback()`. The ISR
   only gives the semaphore.
5. **Why is the driver hook cheaper?** A frame dropped there never reaches the lwIP mailbox,
   `tcpip_thread` or `ethernet_input()`. Under FreeRTOS that is about 44–48% fewer instructions per
   attack frame.
6. **Did you test with an RTOS?** Yes: FreeRTOS 11.2.0 with ST's priorities and a real interrupt, on
   an emulated Cortex-M33. Not on the board.
7. **How did you measure without hardware?** QEMU instruction counts with calibration (10 NOPs = 10),
   deterministic and repeatable. They are a lower bound on cycles.
8. **What did you find in ST's code?** D5: the RX loop stops after a dropped frame. With an early
   filter under load, that loses most legit traffic. We fixed the loop and measured the difference.
9. **How are rules updated without reflashing?** An XVM1 message on UDP port 9000: CRC check, then the
   verifier and the `ctx` check, then a double-buffered install. Sender authentication is future work.
10. **Limitations?** No hardware timing; fixed-offset parsing (no IP options, VLAN or fragment
    handling); the flood results are a model; updates are unauthenticated.
