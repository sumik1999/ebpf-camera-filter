# Airport eBPF Defense – Security Camera Edition

**XDP Berkeley Packet Filter edition** — embedded Linux build for Raspberry Pi 4 with eBPF-based network security.

> 🚀 **Setup Guide:** See [SETUP-GUIDE.md](SETUP-GUIDE.md) for step-by-step build & deployment instructions.
> 📖 **Full Report:** See [SECURITY-CAMERA-BUILD-REPORT.md](SECURITY-CAMERA-BUILD-REPORT.md) for technical details.

## 🚀 Quick Setup (10 Minutes)

### macOS
```bash
./build-docker.sh   # Builds the image automatically
```

### Windows
```powershell
.\build.ps1         # Builds the image automatically
```

Then follow [SETUP-GUIDE.md](SETUP-GUIDE.md) to flash SD card and deploy.

## 🐳 Quick Start (Windows / macOS / Linux)

```bash
# Build the image using Docker (works on any OS)
./build.ps1                    # Windows (PowerShell)
./build-docker.sh               # macOS/Linux (Bash)
```

Or manually:
```bash
docker build -t airport-camera-builder -f Dockerfile .
docker run --rm -v $(pwd):/build airport-camera-builder
```

## What changed from WSL2

| Aspect | WSL2 (original) | Mac / Lima (this port) |
|---|---|---|
| Primary eBPF hook | TC ingress (`SEC("tc")`) | **XDP** (`SEC("xdp")`) |
| Packet context | `struct __sk_buff *skb` | `struct xdp_md *ctx` |
| Drop return code | `TC_ACT_SHOT` | `XDP_DROP` |
| Pass return code | `TC_ACT_OK` | `XDP_PASS` |
| Attach command | `tc filter add … bpf` | `ip link set … xdp obj … sec xdp` |
| Architecture auto-detect | Assumed x86\_64 | Auto (x86\_64 or arm64/Apple Silicon) |
| bpf\_helpers include | Manual stubs | `<bpf/bpf_helpers.h>` (libbpf) |
| Fallback | N/A | TC ingress if XDP fails |
| Offload prep | None | ethtool GRO-off for clean XDP frames |

## What is XDP / Berkeley Packet Filter (BPF)?

**BSD BPF (1993)** – Berkeley's original "Berkeley Packet Filter" from McCanne &
Jacobson introduced a small virtual machine embedded in the kernel that lets user-space
programs install packet filters without copying every packet to user space. It is the
engine behind `tcpdump` and `libpcap`.

**Linux eBPF (2014–present)** – An extended 64-bit successor with maps, JIT, and
multiple hook points. eBPF programs can be attached at:

- **XDP** – the NIC driver's very first receive point (before sk\_buff allocation)
- **TC ingress/egress** – inside the Traffic Control layer
- **kprobes / tracepoints / socket filters** – many other hooks

**XDP (eXpress Data Path)** is the fastest eBPF hook. It runs before the kernel
allocates an `sk_buff` struct, which means packet drops are essentially zero-copy and
zero-allocation. On virtual NICs (veth), XDP runs in "generic/SKB" mode which is
slightly slower but still faster than TC.

## Packet parsing (Berkeley-style)

`ebpf/common.h → parse_packet()` walks the wire format layer by layer:

```
xdp_md { data, data_end }
   ↓
struct ethhdr  → check ETH_P_IP
   ↓
struct iphdr   → check IHL, extract src/dst/proto
   ↓
struct udphdr / struct tcphdr → extract dport, detect SYN
```

Every pointer is bounds-checked against `ctx->data_end`. The eBPF verifier enforces
these checks statically at load time – a direct descendent of the safety guarantees
in the original BSD BPF design.

## Prerequisites (inside Lima / OrbStack Linux guest)

```bash
sudo apt update
sudo apt install -y \
    clang llvm libbpf-dev \
    linux-headers-$(uname -r) linux-libc-dev \
    iproute2 nftables bridge-utils \
    python3 bpftool ethtool
```

> **Apple Silicon (M1/M2/M3):** Lima uses an aarch64 Linux guest.
> The `attach_xdp.sh` script auto-detects this and passes
> `-D__TARGET_ARCH_arm64` to clang.

## Quick start

```bash
# 1. Open Lima shell
limactl shell default

# 2. Clone / copy project
cd ~

# 3. Check environment
sudo make check

# 4. Run full stress matrix
sudo make stress-matrix

# 5. Visual demo
sudo make visual-demo

# 6. Copy HTML to macOS desktop
limactl copy default:~/airport-ebpf-mac/eval/results/visual_replay.html \
    ~/Desktop/visual_replay.html
open ~/Desktop/visual_replay.html
```

## Defense cases

| CASE | Hook | Description |
|---|---|---|
| `no_defense` | — | Baseline, no filtering |
| `linux_filter` | nftables | Conventional firewall baseline |
| `xdp_cms_ebpf` | **XDP** | Count-Min Sketch rate limiter |
| `xdp_elastic_ebpf` | **XDP** | ElasticSketch-inspired (heavy+light separation) |

Aliases `cms_ebpf` and `elastic_ebpf` are accepted for backwards compatibility.

## Running a single case

```bash
sudo CASE=xdp_elastic_ebpf ATTACK_PPS=3000 bash scripts/run_experiment.sh
```

## XDP attach mode

Set `XDP_MODE` in `config.env`:

| Value | Meaning |
|---|---|
| `skb` (default) | Generic/SKB mode – works on all virtual interfaces |
| `native` | Driver-level – requires NIC driver XDP support |
| `offload` | NIC offload – requires SmartNIC |

For Lima/OrbStack veth pairs, `skb` (generic) mode is always available.

## Repository layout

```
airport-ebpf-mac/
├── config.env                    # Mac/Lima-adapted configuration
├── Makefile
├── ebpf/
│   ├── maps.h                    # Shared map / struct definitions
│   ├── common.h                  # XDP packet parser (Berkeley BPF heritage)
│   ├── xdp_cms_filter.bpf.c     # Count-Min Sketch XDP filter  ← NEW
│   └── xdp_elastic_filter.bpf.c # ElasticSketch XDP filter     ← NEW
├── scripts/
│   ├── check_env.sh              # Pre-flight for Lima/OrbStack
│   ├── setup_ns_tree.sh          # Network namespace topology (XDP-ready)
│   ├── attach_xdp.sh             # Primary: XDP attach + auto arch detect ← NEW
│   ├── attach_tc_fallback.sh     # Fallback: TC ingress                   ← NEW
│   ├── detach_xdp.sh             # Detach XDP (and TC fallback)           ← NEW
│   ├── cleanup_ns.sh             # Tear down topology
│   ├── run_experiment.sh         # Single experiment
│   ├── run_stress_matrix.sh      # Full matrix
│   ├── run_visual_demo.sh        # Visual demo
│   └── show_topology.sh          # Print IP table
├── filters/
│   ├── nftables_guard.sh         # nftables linux_filter case
│   └── clear_nftables.sh
├── traffic/                      # Python traffic generators (unchanged)
└── eval/                         # Python metrics & visualisation (unchanged)
```

## Limitations (Mac/Lima vs production)

- Lima/OrbStack VMs use virtual Ethernet; XDP runs in SKB/generic mode.
  Native-mode XDP requires a physical NIC with driver support.
- Network namespaces are faster than real switched networks.
- Telemetry loss remained zero in all runs; loss-recovery was not demonstrated.
- Production deployment requires testing on ARM64 smart-camera hardware
  or an industrial Linux gateway.
