# RAPID Implementation Summary

The earlier adaptive-CMS prototype has been replaced by a network-segmentation implementation of RAPID Algorithm 1.

## Implemented

- SSN topology and switch/port roles in `config/rapid_topology.json`.
- TC/eBPF ingress and egress counters shared across every switch port.
- Thesis `T or N` trigger: 500 ms or 1000 packets, whichever occurs first.
- Ring-buffer anomaly event and immediate `blocked_ports` map update/`TC_ACT_SHOT` drop.
- Signed thesis metrics:
  - `Delta1 = (C21 + C22 - R2in) / (C21 + C22 + R2in)` using paired transit counters.
  - `Delta2 = (Pprx - Pptx) / (Pprx + Pptx)` per port.
- Thresholds 0.15, 0.33, and 0.5 from the thesis.
- Sorted switch/port investigation.
- Immediate guarded edge-port blocking in the TC/eBPF fast path; nftables remains a userspace fallback.
- Transit-port policing with `tc` and topology traceback events.
- Trusted-edge administrator-action handling.
- Consecutive-window and minimum-volume false-positive controls.
- Integration as the `rapid_network` experiment case and stress-matrix case.
- CSV/JSON decision records and controller unit tests.

## Architecture note

The airport testbed has two Linux bridges joined by a router rather than an eight-table OpenFlow pipeline. The bridges represent switches and their `/24` networks represent RAPID sub-subnets. Paired veth counters provide the flow-stat inputs that OpenFlow would provide in the thesis deployment.

CMS and ElasticSketch remain independent fixed-threshold comparison defenses. They are no longer incorrectly described as RAPID or coupled to a zero-initialized adaptive map.

## Validation

```bash
make rapid-test
make validate
sudo make rapid-run       # Linux VM required
```

A live run is needed to validate nftables/`tc` behavior on the target Linux kernel; macOS can run only the unit and syntax tests.
