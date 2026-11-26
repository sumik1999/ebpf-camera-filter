#!/usr/bin/env python3
"""Wrap an ISA-v2 XVM1 metadata program in an authenticated FMV2 update.

Usage:
  bcgen.py examples/comprehensive_meta.vasm -o comprehensive.xvm
  filter_pack.py comprehensive.xvm -o update.fmv2 \
      --device 0x12345678 --generation 1 \
      --helpers 0x2 --key-hex 001122...
"""
import argparse
import binascii
import hashlib
import hmac
import struct
import zlib

FORMAT_VERSION = 1
META_ABI_VERSION = 1
VM_CTX_META = 3
VM_ISA_V2 = 2


def main():
    p = argparse.ArgumentParser()
    p.add_argument("input")
    p.add_argument("-o", "--output", required=True)
    p.add_argument("--device", required=True, type=lambda x: int(x, 0))
    p.add_argument("--generation", required=True, type=lambda x: int(x, 0))
    p.add_argument("--helpers", default=0, type=lambda x: int(x, 0))
    p.add_argument("--key-hex", required=True)
    args = p.parse_args()

    raw = open(args.input, "rb").read()
    if len(raw) < 12 or raw[:4] != b"XVM1":
        raise SystemExit("input is not an XVM1 message")
    isa, ctx, count, crc = struct.unpack_from("<BBHI", raw, 4)
    body = raw[12:]
    if isa != VM_ISA_V2 or ctx != VM_CTX_META:
        raise SystemExit("input must be ISA-v2 ctx-meta bytecode")
    if len(body) != count * 8 or zlib.crc32(body) != crc:
        raise SystemExit("invalid XVM1 length or CRC")
    key = binascii.unhexlify(args.key_hex)
    if not 1 <= len(key) <= 32:
        raise SystemExit("key must be 1..32 bytes")

    header = struct.pack("<4sBBBBHHIIII", b"FMV2", FORMAT_VERSION, isa, ctx,
                         META_ABI_VERSION, count, 0, args.generation,
                         args.device, args.helpers, zlib.crc32(body))
    assert len(header) == 28
    tag = hmac.new(key, header + body, hashlib.sha256).digest()
    open(args.output, "wb").write(header + tag + body)


if __name__ == "__main__":
    main()
