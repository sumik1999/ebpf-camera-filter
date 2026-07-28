#!/usr/bin/env python3
"""Manage the pinned XDP exact, prefix, and Bloom deny-list maps."""

import argparse
import ipaddress
import json
import os
import struct
import subprocess
import sys
from pathlib import Path

BLOOM_BITS = 8192
BLOOM_WORDS = BLOOM_BITS // 64
BLOOM_SEEDS = (0x9E3779B9, 0x85EBCA6B, 0xC2B2AE35)
MAP_NAMES = ("exact_deny", "prefix_deny", "bloom_bits", "defense_cfg")


def run_bpftool(*args, json_output=False, check=True):
    command = ["bpftool"]
    if json_output:
        command.append("-j")
    command.extend(str(arg) for arg in args)
    result = subprocess.run(command, text=True, capture_output=True)
    if check and result.returncode:
        message = result.stderr.strip() or result.stdout.strip()
        raise RuntimeError(f"{' '.join(command)}: {message}")
    if json_output and result.returncode == 0:
        return json.loads(result.stdout)
    return result


def byte_list(value):
    if isinstance(value, dict):
        value = value.get("value", value)
    output = bytearray()
    for item in value:
        output.append(int(item, 0) if isinstance(item, str) else int(item))
    return bytes(output)


def hex_args(data):
    return [f"{byte:02x}" for byte in data]


class Blocklist:
    def __init__(self, pin_root):
        self.pin_root = Path(pin_root)

    def path(self, name):
        return self.pin_root / name

    def require_maps(self):
        missing = [str(self.path(name)) for name in MAP_NAMES if not self.path(name).exists()]
        if missing:
            raise RuntimeError(
                "blocklist maps are not pinned; attach xdp_frontend first. Missing: "
                + ", ".join(missing)
            )

    def update(self, name, key, value):
        run_bpftool(
            "map", "update", "pinned", self.path(name),
            "key", "hex", *hex_args(key),
            "value", "hex", *hex_args(value),
            "any",
        )

    def delete(self, name, key):
        run_bpftool(
            "map", "delete", "pinned", self.path(name),
            "key", "hex", *hex_args(key),
        )

    def lookup(self, name, key):
        result = run_bpftool(
            "map", "lookup", "pinned", self.path(name),
            "key", "hex", *hex_args(key), json_output=True,
        )
        return byte_list(result["value"])

    def exact_key(self, address):
        return ipaddress.IPv4Address(address).packed

    def prefix_key(self, prefix):
        network = ipaddress.IPv4Network(prefix, strict=False)
        return struct.pack("=I", network.prefixlen) + network.network_address.packed

    @staticmethod
    def mix(value):
        value ^= value >> 16
        value = (value * 0x7FEB352D) & 0xFFFFFFFF
        value ^= value >> 15
        value = (value * 0x846CA68B) & 0xFFFFFFFF
        value ^= value >> 16
        return value & 0xFFFFFFFF

    def bloom_locations(self, address):
        source = int(ipaddress.IPv4Address(address))
        for seed in BLOOM_SEEDS:
            bit = self.mix(source ^ seed) & (BLOOM_BITS - 1)
            yield bit // 64, bit % 64

    def set_bloom_enabled(self, enabled):
        self.update("defense_cfg", struct.pack("=I", 0), struct.pack("=I", int(enabled)))

    def add_bloom(self, address):
        for word_index, bit_index in self.bloom_locations(address):
            key = struct.pack("=I", word_index)
            word = struct.unpack("=Q", self.lookup("bloom_bits", key))[0]
            word |= 1 << bit_index
            self.update("bloom_bits", key, struct.pack("=Q", word))
        self.set_bloom_enabled(True)

    def clear_bloom(self):
        zero = struct.pack("=Q", 0)
        for word_index in range(BLOOM_WORDS):
            self.update("bloom_bits", struct.pack("=I", word_index), zero)
        self.set_bloom_enabled(False)

    def status(self):
        exact = run_bpftool("map", "dump", "pinned", self.path("exact_deny"), json_output=True)
        prefixes = run_bpftool("map", "dump", "pinned", self.path("prefix_deny"), json_output=True)
        config = struct.unpack("=I", self.lookup("defense_cfg", struct.pack("=I", 0)))[0]
        bloom = run_bpftool("map", "dump", "pinned", self.path("bloom_bits"), json_output=True)
        set_bits = 0
        for entry in bloom:
            word = struct.unpack("=Q", byte_list(entry["value"]))[0]
            set_bits += word.bit_count()
        print(f"pin_root={self.pin_root}")
        print(f"exact_entries={len(exact)}")
        print(f"prefix_entries={len(prefixes)}")
        print(f"bloom_enabled={config}")
        print(f"bloom_set_bits={set_bits}/{BLOOM_BITS}")


def parser():
    result = argparse.ArgumentParser(description=__doc__)
    result.add_argument(
        "--pin-root",
        default=os.environ.get("AIRPORT_BPF_PIN_DIR", "/sys/fs/bpf/airport-defense"),
    )
    groups = result.add_subparsers(dest="group", required=True)

    for group in ("exact", "prefix"):
        sub = groups.add_parser(group).add_subparsers(dest="action", required=True)
        add = sub.add_parser("add")
        add.add_argument("value")
        delete = sub.add_parser("delete")
        delete.add_argument("value")

    bloom = groups.add_parser("bloom").add_subparsers(dest="action", required=True)
    bloom_add = bloom.add_parser("add")
    bloom_add.add_argument("value")
    bloom.add_parser("clear")
    bloom.add_parser("enable")
    bloom.add_parser("disable")
    groups.add_parser("status")
    return result


def main():
    args = parser().parse_args()
    blocklist = Blocklist(args.pin_root)
    blocklist.require_maps()

    if args.group == "exact":
        key = blocklist.exact_key(args.value)
        if args.action == "add":
            blocklist.update("exact_deny", key, b"\x01")
        else:
            blocklist.delete("exact_deny", key)
    elif args.group == "prefix":
        key = blocklist.prefix_key(args.value)
        if args.action == "add":
            blocklist.update("prefix_deny", key, b"\x01")
        else:
            blocklist.delete("prefix_deny", key)
    elif args.group == "bloom":
        if args.action == "add":
            blocklist.add_bloom(args.value)
        elif args.action == "clear":
            blocklist.clear_bloom()
        else:
            blocklist.set_bloom_enabled(args.action == "enable")
    else:
        blocklist.status()
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (RuntimeError, ValueError) as error:
        print(f"blocklistctl: {error}", file=sys.stderr)
        raise SystemExit(1)
