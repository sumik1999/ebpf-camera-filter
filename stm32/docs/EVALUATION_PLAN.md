# Evaluation plan

## Research questions
- **RQ1 (placement):** How much CPU time and latency does dropping at the driver layer save,
  compared with dropping at lwIP's `ip4_input()`?
- **RQ2 (interpretation cost):** What does a verified bytecode VM cost compared with native C for the
  same filter on a Cortex-M33? Is that cost per-packet (entry, checks) or per-instruction (dispatch)?
- **RQ3 (verification for speed):** Can static analysis (verifier-computed minimum frame length)
  remove runtime bounds checks, and how much does that save?
- **RQ4 (survivability):** Under a UDP flood of rising rate, how much legitimate traffic
  (goodput, RTT) survives in each configuration? Where does receive livelock start?

## Configurations (`EXP_CONFIG` compile-time switch)

| ID | Filter | Where | Purpose |
|---|---|---|---|
| C0 | none | — | Baseline lwIP behaviour under flood |
| C1 | native C | lwIP `ip4_input` hook | Late placement, no interpretation |
| C2 | VM bytecode | lwIP `ip4_input` hook | Late placement, interpreted |
| C3 | native C | driver hook (`RxLinkCallback`) | Early placement, no interpretation |
| C4 | VM bytecode | driver hook (`RxLinkCallback`) | **Proposed system** |
| C5 | MAC hardware L3/L4 filter | Ethernet MAC | Fixed-function lower bound (0 CPU) |
| C6 | VM bytecode | ISR (experimental) | Is ISR placement worth the latency risk? |

C1 vs C3 isolates the **placement effect**; C3 vs C4 isolates the **interpretation overhead**.
C4 vs C5 shows what programmability costs relative to fixed-function hardware.

## Metrics and how to measure

| Metric | Method |
|---|---|
| Cycles per verdict | DWT `CYCCNT` read before and after the filter call (enable: `DCB->DEMCR |= DCB_DEMCR_TRCENA_Msk; DWT->CYCCNT = 0; DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;`. Older CMSIS spells `DCB` as `CoreDebug`); log min/mean/p99/max |
| ISR → verdict latency | GPIO high at ETH ISR entry, low at verdict; logic analyser or scope; histogram |
| CPU load | FreeRTOS run-time stats per task (`configGENERATE_RUN_TIME_STATS`, DWT or TIM timebase) plus an idle-hook counter |
| Throughput / drops | Frames seen, passed and dropped (hook counters); RX overruns (`ETH->DMACMFCR`); legit goodput via iperf (UDP/TCP) |
| Legit latency under attack | `ping` RTT (or a UDP echo timestamp tool) during floods |
| Flash / RAM footprint | `arm-none-eabi-size -A`, `.map` file, `-fstack-usage`; task stacks via `uxTaskGetStackHighWaterMark()` |
| RTOS timing overhead | RX-task execution time per frame, worst-case response time of a periodic control task under flood |

## Traffic
- PC ↔ board on a **direct cable** (or a switch with known behaviour). Record NIC, OS and tool versions.
- Flood: scapy/hping3 for moderate rates. For line-rate small frames use Linux `pktgen` or `trafgen`.
- Line-rate budget at 100 Mbit/s: a 64-B frame occupies 84 B on the wire (preamble + IFG), so
  ≈ **148,800 frames/s**. At 250 MHz that leaves ≈ **1,680 CPU cycles per frame**. Any per-frame
  path longer than that cannot keep up at line rate (receive livelock, Mogul & Ramakrishnan).
- Sweep the flood rate (e.g. 1k, 5k, 10k, 25k, 50k, 100k, max pps), at least 3 runs each, 30–60 s per run.

## Reporting rules
- Raw CSV + script + config + commit hash + date for every figure.
- Report mean **and** spread (p99/max). Worst case matters for an RTOS.
- Say plainly where the VM is slower than native C. The thesis argument is about *where* filtering
  happens and *safe programmability*, not about beating native C.
