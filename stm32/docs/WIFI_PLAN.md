# Wi-Fi network hook: analysis and plan (branch `wifi-hook`)

Status: **proposal, not implemented.** Written 24 Nov 2026. Same rules as the rest of the project:
no hardware, everything emulated, never present simulated numbers as hardware results.

## 1. How Wi-Fi is attached to an STM32

The STM32H563 has no Wi-Fi radio. Wi-Fi comes from an external module, connected over SPI, SDIO or
UART. There are two architectures, and only one of them allows a packet hook on the STM32.

| Architecture | Who runs TCP/IP | What the STM32 receives | Frame-level hook on the STM32? |
|---|---|---|---|
| **A. Socket offload** ("network processor", AT commands; e.g. ESP-AT; EMW3080 in socket mode, `MX_WIFI_NETWORK_BYPASS_MODE == 0`) | the module | socket data only (after the module's TCP/IP stack) | **No.** Packets are filtered and answered inside the module. Only an application-level filter on socket data is possible. |
| **B. Host stack** ("bypass", "network split", "802.3 netif"; e.g. ESP-Hosted FG/MCU on an ESP32; EMW3080 bypass mode) | **the STM32 (lwIP)** | **802.3 Ethernet frames** over SPI/SDIO; the module does the 802.11 MAC/PHY, WPA and the 802.11↔802.3 conversion | **Yes**, in the host driver's RX path, before `netif->input()` |

**Conclusion:** the project only makes sense in architecture B. In B, the frames reaching the STM32
are already in **Ethernet II format** (dst MAC, src MAC, EtherType). So our `VM_CTX_ETH` program,
the verifier and the native filter work **unchanged**: same offsets 12, 23 and 36.

### What was checked (24 Nov 2026, web search; to be confirmed by reading the driver source in step W1)
- **ESP-Hosted (Espressif):**
  - FG/MCU variants give the host an **802.3 Ethernet interface**;
  - STM32 hosts are documented (STM32F469I-DISCO, NUCLEO-F412ZG), over SPI, SDIO or UART;
  - in the network-split examples, **the host runs lwIP** and the co-processor forwards 802.3 frames
    into the host netif.
  - Sources: github.com/espressif/esp-hosted; components.espressif.com `esp_hosted` network_split examples.
- **MXCHIP EMW3080 (ST B-U585I-IOT02A, `mx_wifi` driver):**
  - the driver has `MX_WIFI_NETWORK_BYPASS_MODE` (0 = socket mode);
  - ST forum posts say NetXDuo needs the module in data (bypass) mode;
  - a working **bypass + lwIP** setup is **not confirmed** by what we found;
  - socket mode has open issues (non-blocking sockets).
  - Sources: community.st.com threads; st.com X-WIFI-EMW3080B.
- Nothing here has been checked on hardware.

**Recommended reference: ESP32 + ESP-Hosted (SPI, host lwIP).**
- It is open source, so the driver can be read like we read ST's `ethernetif.c`.
- STM32 hosts are documented.
- It delivers 802.3 frames.

The EMW3080 is the alternative if an ST-ecosystem module is preferred, but its bypass + lwIP path
must be verified first.

## 2. RX path: Ethernet (done) vs Wi-Fi host stack (planned)

| Step | Ethernet (ST, done) | Wi-Fi host stack (ESP-Hosted style, to model) |
|---|---|---|
| Frame arrives | MAC writes it into an RX descriptor's buffer by **DMA** (zero-copy `RX_POOL`) | the module receives 802.11, decrypts, converts to 802.3 and queues it **on the module** |
| Signal | ETH IRQ, one per frame | **data-ready GPIO IRQ** (handshake line) from the module |
| ISR | gives `RxPktSemaphore` | gives the transport task's semaphore (same pattern) |
| Task | RX task: `HAL_ETH_ReadData()`, one frame per call | **SPI transport task**: one SPI (DMA) transaction reads a transport buffer (header + payload) |
| Buffer | zero-copy custom pbuf from `RX_POOL` | payload usually **copied** into a pbuf (to verify in W1) |
| **Our hook** | `HAL_ETH_RxLinkCallback()`, before linking | **after the SPI read, before the pbuf alloc/copy and `netif->input()`** |
| Then | `tcpip_input()` → mailbox → `tcpip_thread` | same (`tcpip_input()` → mailbox → `tcpip_thread`) |
| Late hook | `ip4_input()` hook | **identical** (lwIP does not care which netif) |

**What an early drop saves on Wi-Fi:**
- **saved:** the pbuf allocation and copy, the mailbox post, the switch to `tcpip_thread`, and lwIP
  processing;
- **not saved:** the module's radio and decryption work, and the **SPI bus transfer** (the frame has
  already crossed the bus when the host can look at it).

So on Wi-Fi the early hook still saves CPU, but the **SPI bus becomes the limit under a flood**. Real
protection of the bus needs the filter to run **on the module** (the ESP32 could run our VM: an
"offload" variant, listed as future work).

## 3. Reuse

| Component | Reuse | Change |
|---|---|---|
| VM: ISA, verifier, interpreter, `vm_slot`, `vm_loader` | **unchanged** | none (ISA v1 stays frozen) |
| DDoS bytecode (ETH version) and native C filter | **unchanged** | none: same 802.3 offsets |
| `hook_common.h` | **unchanged** | none |
| Driver hook | pattern reused | new thin wrapper `wifi_rx_hook.[ch]`: its own `hook_t` (own slot and counters), call site documented for the Wi-Fi driver |
| lwIP hook and glue | **unchanged** | none |
| `EXP_CONFIG` C0–C4 | meaning unchanged | in the Wi-Fi build, "driver hook" = the Wi-Fi RX hook |
| Loader / control port | **unchanged** | runs over Wi-Fi like any UDP traffic |
| lwIP 2.2.1, `lwipopts.h` | reused | Wi-Fi netif (MTU 1500, own MAC) |
| FreeRTOS config, `rtos_main.c` pattern | reused | transport task instead of the ETH RX task; data-ready IRQ instead of ETH IRQ |
| Traces (`gen_traces.py`) | **reused as-is** | frames are 802.3 on the host side; optional Wi-Fi-typical extras (multicast mDNS/SSDP) |
| Runner, flood model, plots | reused | new build `wifi_rtos`; the flood model adds an SPI-bandwidth limit |
| Tests | reused | new tests for `wifi_rx_hook` |
| **New** | — | `sim/wifi_sim/`: a module + SPI transport model and the host transport driver (mirrors ESP-Hosted after W1) |

## 4. Wi-Fi-specific points to state in the thesis
1. **Hook position:** after the SPI transfer, so bus time is not saved (see §2).
2. **Socket-offload modules cannot host this framework.** It needs host-stack mode.
3. **802.11 management attacks** (deauthentication or beacon floods) are handled inside the module
   and are invisible to the STM32. They are out of scope.
4. **SSDP (port 1900) is legitimate multicast on Wi-Fi LANs.** Dropping all of port 1900 also drops
   legit discovery. A better rule would match unicast traffic to our IP only, or a rate limit (ISA v2).
5. **Throughput** is limited by the SPI clock and the transport protocol, not by the 100 Mbit/s
   Ethernet line rate. Use the driver's documented figures, cited, and do not invent numbers.
6. **Copy vs zero-copy:** if the Wi-Fi driver copies every frame, an early drop also saves that copy.
   Measure it.

## 5. Plan (on branch `wifi-hook`; merge into `main` only with your OK)

| Step | Who | What | Done when |
|---|---|---|---|
| W1 | role2 | Clone ESP-Hosted (pinned tag) into `~/Projects/vendor/` (outside the repo). Read the STM32/MCU host RX path. Record facts in `docs/LOG.md`: transport header format, buffer sizes, copy vs zero-copy, task priorities and stacks, how frames reach `netif->input`. | facts written down, each with file:line |
| W2 | role2 | `firmware/Network_Hooks/wifi_rx_hook.[ch]`: `wifi_hook_run(frame, len)` on its own `hook_t` (ctx ETH); call site documented. Host tests (refuses the IPv4 program, drops the attack, independent of the Ethernet hook). | tests pass on the Mac, under ASan, on QEMU |
| W3 | role2 | `sim/wifi_sim/`: a module model (on-module RX queue of N frames, loss counter when full) and a SPI transport model (data-ready IRQ, one transport buffer per transaction, bytes counted). The host transport driver mirrors W1, with the hook before the pbuf alloc/copy. | the same traces give the same verdicts as the Ethernet build |
| W4 | role2 | FreeRTOS build for Wi-Fi: transport task and data-ready IRQ, reusing `sim/rtos`. | `make wifirun` works; no asserts or stack overflow |
| W5 | joint | Runner: add the `wifi_rtos` build (functional matrix + per-frame counts + SPI bytes per frame). | recorded in `eval/raw/<date>_<commit>_sim/` |
| W6 | joint | Compare Ethernet with Wi-Fi: cost of an early vs late drop; copy cost; flood model with an SPI-bandwidth cap. One or two figures. | figures generated from CSV |
| W7 | role1 | (optional) A unicast-only SSDP rule in bytecode (`bcgen`), loaded over the control port, to show a Wi-Fi-appropriate rule. | demo check passes |
| W8 | joint | Docs: `WALKTHROUGH_SIMPLE.md` section, `KNOWN_ISSUES` (Wi-Fi limitations), plan ticks. | committed on the branch |

**Effort:** about one working day (W1–W6), plus about 2 hours for W7–W8.
**Schedule:** Day 2 finished early. So Wi-Fi fits on 25 Nov, and the report and slides stay on 26 Nov.

## 6. Decisions needed
1. **Reference module:** ESP32 + ESP-Hosted over SPI (recommended), or MXCHIP EMW3080 (ST; bypass
   mode not yet confirmed).
2. **In the report:** a main result, or a "portability" chapter? Recommended: portability. The frozen
   resume bullets don't mention Wi-Fi.
