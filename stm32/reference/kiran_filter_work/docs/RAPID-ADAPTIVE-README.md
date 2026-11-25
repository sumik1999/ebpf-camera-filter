# RAPID Network Segmentation

This project implements Algorithm 1 from Himanshu's RAPID thesis for the Linux namespace airport topology. It is a switch-port controller, not an adaptive CMS packet-rate threshold.

## Mapping to the thesis

| RAPID concept | Implementation |
|---|---|
| SDN switch | Linux bridge (`br_terminal`, `br_camera`) |
| Sub-subnet (SSN) | `10.10.0.0/24`, `10.20.0.0/24` |
| Switch port | Root-namespace veth bridge port |
| Flow-stat trigger | TC/eBPF evaluates every `T=500 ms` or `N=1000` packets, whichever occurs first |
| `Delta1` link balance | Local transit RX versus peer transit TX |
| `Delta2` port balance | `(port_rx - port_tx) / (port_rx + port_tx)` |
| Edge mitigation | Immediate `TC_ACT_SHOT` through the shared `blocked_ports` BPF map |
| Transit mitigation | `tc` police meter and topology traceback |

The topology and trust policy are in `config/rapid_topology.json`. APOC and camera ports are trusted; only the simulated attacker port is inserted into the BPF `guarded_ports` map and is eligible for automatic blocking. The slower userspace controller remains responsible for topology analysis and records decisions every 500 ms.

## Run

Inside the Linux VM:

```bash
sudo make setup
sudo make rapid-attach       # live controller; Ctrl-C to stop
```

Run a complete experiment:

```bash
make rapid-run
```

Run the comparison matrix:

```bash
make rapid-matrix
```

Outputs for an experiment include:

- `rapid_metrics.csv`: decisions and the exact signed `Delta1`/`Delta2` values.
- `rapid_events.json`: block, meter, traceback, or administrator-action events.
- `defense.log`: live controller log.
- `summary.json`: application-level experiment results.

## Safety controls

The fast path evaluates a guarded port when either 500 ms expires or 1000 packets arrive. If at least 50 packets were observed and `Delta2 > 0.5`, it inserts the ifindex in `blocked_ports`, emits a ring-buffer event, and drops the triggering packet and all subsequent ingress packets. Configure it with:

```bash
RAPID_WINDOW_NS=500000000 RAPID_PACKET_TRIGGER=1000 RAPID_MIN_PACKETS=50 make rapid-run
```

`RAPID_WINDOW_NS` is in nanoseconds; the default is `500000000`. Trusted edge and transit ports collect counters but cannot self-block.

## Tests

```bash
make rapid-test
make validate
```

The controller requires root for nftables and traffic-control actions. `--dry-run` computes decisions without changing networking.
