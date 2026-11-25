#!/usr/bin/env python3
"""gen_traces.py -- deterministic packet traces for the simulated firmware.

Writes standard .pcap files (open them in Wireshark) plus a <name>.labels.csv
saying what each frame is. Plain Python, no scapy. Same output every run
(fixed random seed), so experiments are reproducible.

Network (matches the simulated firmware):
  device  192.168.10.2   MAC 00:80:e1:00:05:77  (ST example MAC)
  PC      192.168.10.1   MAC 02:00:00:00:00:01  (also the gateway)
  attack  spoofed sources 10.x.y.z, UDP to port 1900 (SSDP reflection port)

Frame kinds: arp_req, echo (UDP port 7, answered), ping (ICMP echo),
dns (UDP port 53, nobody listens), attack (UDP port 1900), tcp (SYN to
port 80; TCP is off in the sim), ipv6, and odd frames (VLAN-tagged attack,
attack with IP options, non-first fragment).

Usage: gen_traces.py <out_dir>
"""
import os
import random
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools", "asm"))
import bcgen  # noqa: E402  (the rulebook assembler, for the live-update demo)

DEV_MAC = bytes.fromhex("0080e1000577")
PC_MAC = bytes.fromhex("020000000001")
DEV_IP = bytes([192, 168, 10, 2])
PC_IP = bytes([192, 168, 10, 1])
TARGET_PORT = 1900


def csum(data):
    if len(data) % 2:
        data += b"\0"
    s = sum(struct.unpack(f"!{len(data) // 2}H", data))
    while s >> 16:
        s = (s & 0xFFFF) + (s >> 16)
    return (~s) & 0xFFFF


def eth(dst, src, ethertype, payload):
    f = dst + src + struct.pack("!H", ethertype) + payload
    return f + b"\0" * max(0, 60 - len(f))          # minimum frame size (without FCS)


def ipv4(src, dst, proto, payload, ident=1, frag=0x4000, options=b""):
    ihl = 5 + len(options) // 4
    hdr = struct.pack("!BBHHHBBH4s4s", 0x40 | ihl, 0, 4 * ihl + len(payload), ident, frag, 64, proto, 0, src, dst)
    hdr += options
    hdr = hdr[:10] + struct.pack("!H", csum(hdr)) + hdr[12:]
    return hdr + payload


def udp(src_ip, dst_ip, sport, dport, data):
    seg = struct.pack("!HHHH", sport, dport, 8 + len(data), 0) + data
    pseudo = src_ip + dst_ip + struct.pack("!BBH", 0, 17, len(seg))
    c = csum(pseudo + seg) or 0xFFFF
    return seg[:6] + struct.pack("!H", c) + seg[8:]


def icmp_echo(ident, seq, data=b"abcdefghijklmnopqrstuvwabcdefghi"):
    h = struct.pack("!BBHHH", 8, 0, 0, ident, seq) + data
    return h[:2] + struct.pack("!H", csum(h)) + h[4:]


def tcp_syn(src_ip, dst_ip, sport, dport):
    seg = struct.pack("!HHIIBBHHH", sport, dport, 1000, 0, 5 << 4, 0x02, 8192, 0, 0)
    pseudo = src_ip + dst_ip + struct.pack("!BBH", 0, 6, len(seg))
    return seg[:16] + struct.pack("!H", csum(pseudo + seg)) + seg[18:]


# Rulebooks for the live-update demo, assembled from text by tools/asm/bcgen.py.
_ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
CTRL = {
    # retarget the filter to UDP port 53
    "port53": bcgen.message(*bcgen.assemble(open(os.path.join(_ROOT, "tools", "asm", "examples", "ddos_port53.vasm")).read())),
    # a broken rulebook (backward jump = possible endless loop): must be refused
    "bad": bcgen.message(*bcgen.assemble("ctx eth\nmov r1, 1\njmp -2\nret 1\n")),
}


class Gen:
    def __init__(self, seed=1):
        self.rng = random.Random(seed)
        self.ident = 1

    def nid(self):
        self.ident = (self.ident + 1) & 0xFFFF
        return self.ident

    def frame(self, kind):
        r = self.rng
        if kind == "arp_req":
            arp = struct.pack("!HHBBH", 1, 0x0800, 6, 4, 1) + PC_MAC + PC_IP + b"\0" * 6 + DEV_IP
            return eth(b"\xff" * 6, PC_MAC, 0x0806, arp)
        if kind == "echo":
            data = bytes(r.randrange(256) for _ in range(32))
            return eth(DEV_MAC, PC_MAC, 0x0800, ipv4(PC_IP, DEV_IP, 17, udp(PC_IP, DEV_IP, 40000, 7, data), self.nid()))
        if kind == "ping":
            return eth(DEV_MAC, PC_MAC, 0x0800, ipv4(PC_IP, DEV_IP, 1, icmp_echo(0x1234, self.nid()), self.nid()))
        if kind == "dns":
            return eth(DEV_MAC, PC_MAC, 0x0800, ipv4(PC_IP, DEV_IP, 17, udp(PC_IP, DEV_IP, 40001, 53, b"\0" * 20), self.nid()))
        if kind == "tcp":
            return eth(DEV_MAC, PC_MAC, 0x0800, ipv4(PC_IP, DEV_IP, 6, tcp_syn(PC_IP, DEV_IP, 40002, 80), self.nid()))
        if kind.startswith("ctrl:"):  # a rulebook sent to the control port 9000
            data = CTRL[kind[5:]]
            return eth(DEV_MAC, PC_MAC, 0x0800, ipv4(PC_IP, DEV_IP, 17, udp(PC_IP, DEV_IP, 40003, 9000, data), self.nid()))
        if kind == "ipv6":
            return eth(DEV_MAC, PC_MAC, 0x86DD, b"\x60" + b"\0" * 39)
        src = bytes([10, r.randrange(256), r.randrange(256), r.randrange(1, 255)])
        ssdp = b"M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\n\r\n"
        if kind == "attack":
            return eth(DEV_MAC, PC_MAC, 0x0800, ipv4(src, DEV_IP, 17, udp(src, DEV_IP, r.randrange(1024, 65535), TARGET_PORT, ssdp), self.nid()))
        if kind == "attack_vlan":     # 802.1Q tag: filter does not parse VLAN (known limitation)
            inner = struct.pack("!HH", 10, 0x0800) + ipv4(src, DEV_IP, 17, udp(src, DEV_IP, 5555, TARGET_PORT, ssdp), self.nid())
            return eth(DEV_MAC, PC_MAC, 0x8100, inner)
        if kind == "attack_ipopt":    # IHL > 5: fixed offsets miss the port (known limitation)
            return eth(DEV_MAC, PC_MAC, 0x0800, ipv4(src, DEV_IP, 17, udp(src, DEV_IP, 5555, TARGET_PORT, ssdp), self.nid(), options=b"\x01\x01\x01\x00"))
        if kind == "frag_tail":       # non-first fragment: no UDP header at the port offset
            return eth(DEV_MAC, PC_MAC, 0x0800, ipv4(src, DEV_IP, 17, b"\x07\x6c" * 20, self.nid(), frag=0x0005))
        raise ValueError(kind)


def write(out_dir, name, kinds, gap_us, expected=None):
    """kinds: list of frame kinds in order; gap_us: microseconds between frames;
    expected: optional list of expected verdicts (default: attack = DROP)."""
    g = Gen()
    path = os.path.join(out_dir, name + ".pcap")
    with open(path, "wb") as f, open(os.path.join(out_dir, name + ".labels.csv"), "w") as lab:
        f.write(struct.pack("<IHHiIII", 0xA1B2C3D4, 2, 4, 0, 0, 65535, 1))   # pcap, Ethernet
        lab.write("index,kind,expected_with_filter\n")
        t = 0
        for i, k in enumerate(kinds):
            fr = g.frame(k)
            f.write(struct.pack("<IIII", t // 1_000_000, t % 1_000_000, len(fr), len(fr)) + fr)
            exp = expected[i] if expected else ("DROP" if k == "attack" else "PASS")
            lab.write(f"{i},{k},{exp}\n")
            t += gap_us
    return path


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "build/traces"
    os.makedirs(out, exist_ok=True)
    rng = random.Random(7)
    legit_kinds = ["echo", "ping", "dns"]

    # 1. legit only: 300 frames, 10 ms apart
    write(out, "legit", ["arp_req"] + [legit_kinds[i % 3] for i in range(300)], 10_000)
    # 2. attack only: 2000 frames, 100 us apart (10,000 frames/s)
    write(out, "attack", ["arp_req"] + ["attack"] * 2000, 100)
    # 3. mixes: echo requests hidden in a flood, attack share 50 %, 90 %, 99 %
    for share in (50, 90, 99):
        n = 3000
        kinds = ["arp_req"] + ["attack" if rng.randrange(100) < share else "echo" for _ in range(n)]
        write(out, f"mix{share}", kinds, 100)
    # 4. odd frames (known limitations of the fixed-offset filter)
    write(out, "odd", ["arp_req"] + ["attack_vlan", "attack_ipopt", "frag_tail", "ipv6", "tcp"] * 20, 1000)
    # 5. measurement trace: a few of each kind, for per-frame instruction counts
    meas = ["arp_req", "echo"] + ["echo", "ping", "dns", "attack", "tcp", "ipv6"] * 3
    write(out, "measure", meas, 1000)
    # 6. live-update demo (config C4): traffic; a broken rulebook (refused);
    #    traffic; a rulebook blocking port 53 (installed); traffic.
    cycle = ["echo", "attack", "dns"]
    p1 = [cycle[i % 3] for i in range(60)]
    p2 = [cycle[i % 3] for i in range(30)]
    p3 = [cycle[i % 3] for i in range(60)]
    kinds = ["arp_req"] + p1 + ["ctrl:bad"] + p2 + ["ctrl:port53"] + p3
    old = lambda k: "DROP" if k == "attack" else "PASS"
    new = lambda k: "DROP" if k == "dns" else "PASS"
    expected = ["PASS"] + [old(k) for k in p1] + ["PASS"] + [old(k) for k in p2] + ["PASS"] + [new(k) for k in p3]
    write(out, "loader_demo", kinds, 1000, expected)
    print(f"traces written to {out}")


if __name__ == "__main__":
    main()
