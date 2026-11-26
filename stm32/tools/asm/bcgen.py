#!/usr/bin/env python3
"""bcgen.py -- minimal assembler for VM ISA v1/v2 (docs/ISA.md).

Turns a readable program into an XVM1 message (the format vm_loader.c
accepts), so new filter rules can be sent to a running device as data.

Syntax (one instruction per line, ';' starts a comment):
    ctx eth | ipv4 | meta          program context (meta selects ISA v2)
    ldb|ldh|ldw rD, [rS+off]       packet load, 1/2/4 bytes, big-endian
    add|sub|and|or|xor|lsh|rsh rD, imm | rS
    mov rD, imm | rS
    jmp +off
    jeq|jne|jgt|jge|jlt|jle rD, imm | rS, +off     jump to pc+1+off if true
    call helper_id                  ISA-v2 bounded helper, result in r0
    ret 0 | 1                       0 = DROP, 1 = PASS

Usage:
    bcgen.py prog.vasm -o prog.xvm     write the message
    bcgen.py prog.vasm --hex           print it in hex
    bcgen.py --selftest
"""
import re
import struct
import sys
import zlib

ISA_V1 = 1
ISA_V2 = 2
CTX = {"eth": 1, "ipv4": 2, "meta": 3}
CLASS = {"ld": 0, "alu_k": 1, "alu_x": 2, "jmp_k": 3, "jmp_x": 4, "ret": 5, "misc": 6, "call": 7}
LOADS = {"ldb": 0, "ldh": 1, "ldw": 2}
ALU = {"add": 0, "sub": 1, "and": 2, "or": 3, "xor": 4, "lsh": 5, "rsh": 6}
JMP = {"jmp": 0, "jeq": 1, "jne": 2, "jgt": 3, "jge": 4, "jlt": 5, "jle": 6}


def op(sub, cls):
    return (sub << 3) | CLASS[cls]


def reg(tok):
    m = re.fullmatch(r"r([0-7])", tok)
    if not m:
        raise ValueError(f"bad register '{tok}' (r0..r7)")
    return int(m.group(1))


def num(tok):
    return int(tok, 0)


def encode(opcode, dst=0, src=0, offset=0, imm=0):
    return struct.pack("<BBhi", opcode, dst | (src << 4), offset, imm)


def assemble(text):
    """Returns (ctx, instruction bytes)."""
    ctx, out = None, b""
    for lineno, raw in enumerate(text.splitlines(), 1):
        line = raw.split(";")[0].strip().lower()
        if not line:
            continue
        mn, _, rest = line.partition(" ")
        args = [a.strip() for a in rest.split(",")] if rest.strip() else []
        try:
            if mn == "ctx":
                ctx = CTX[args[0]]
            elif mn in LOADS:
                m = re.fullmatch(r"\[(r[0-7])\s*([+-]\s*\w+)?\]", args[1].replace(" ", ""))
                if not m:
                    raise ValueError("load needs [rS+off]")
                off = num(m.group(2).replace(" ", "")) if m.group(2) else 0
                out += encode(op(LOADS[mn], "ld"), reg(args[0]), reg(m.group(1)), off)
            elif mn in ALU:
                if args[1].startswith("r"):
                    out += encode(op(ALU[mn], "alu_x"), reg(args[0]), reg(args[1]))
                else:
                    out += encode(op(ALU[mn], "alu_k"), reg(args[0]), imm=num(args[1]))
            elif mn == "mov":
                if args[1].startswith("r"):
                    out += encode(op(1, "misc"), reg(args[0]), reg(args[1]))
                else:
                    out += encode(op(0, "misc"), reg(args[0]), imm=num(args[1]))
            elif mn == "jmp":
                out += encode(op(0, "jmp_k"), offset=num(args[0]))
            elif mn in JMP:
                if args[1].startswith("r"):
                    out += encode(op(JMP[mn], "jmp_x"), reg(args[0]), reg(args[1]), num(args[2]))
                else:
                    out += encode(op(JMP[mn], "jmp_k"), reg(args[0]), offset=num(args[2]), imm=num(args[1]))
            elif mn == "call":
                if ctx != CTX["meta"]:
                    raise ValueError("call is only valid with 'ctx meta'")
                out += encode(op(0, "call"), imm=num(args[0]))
            elif mn == "ret":
                out += encode(op(0, "ret"), imm=num(args[0]))
            else:
                raise ValueError(f"unknown mnemonic '{mn}'")
        except (ValueError, IndexError, KeyError) as e:
            raise SystemExit(f"line {lineno}: {raw.strip()}: {e}")
    if ctx is None:
        raise SystemExit("missing 'ctx eth', 'ctx ipv4', or 'ctx meta'")
    return ctx, out


def message(ctx, body):
    """XVM1 header (magic, ISA version, ctx, count, CRC-32) + instructions."""
    version = ISA_V2 if ctx == CTX["meta"] else ISA_V1
    return b"XVM1" + struct.pack("<BBHI", version, ctx, len(body) // 8, zlib.crc32(body)) + body


DDOS_ETH = """ctx eth
ldh r1, [r0+12]
jne r1, 0x0800, +5
ldb r2, [r0+23]
jne r2, 17, +3
ldh r3, [r0+36]
jne r3, 1900, +1
ret 0
ret 1
"""


def selftest():
    # Encoding example from docs/ISA.md section 3.
    assert encode(0x13, 3, 5, -2, 0x12345678) == bytes.fromhex("1353feff78563412")
    # The DDoS program, field by field as in firmware/DDoS_Prototype/ddos_bytecode.h.
    ctx, body = assemble(DDOS_ETH)
    want = [(0x08, 1, 0, 12, 0), (0x13, 1, 0, 5, 0x0800), (0x00, 2, 0, 23, 0), (0x13, 2, 0, 3, 17),
            (0x08, 3, 0, 36, 0), (0x13, 3, 0, 1, 1900), (0x05, 0, 0, 0, 0), (0x05, 0, 0, 0, 1)]
    assert ctx == 1 and body == b"".join(encode(*w) for w in want), "DDoS program mismatch"
    m = message(ctx, body)
    assert m[:4] == b"XVM1" and len(m) == 12 + 64
    print("bcgen selftest: ok")


def main():
    if "--selftest" in sys.argv:
        selftest()
        return
    src = sys.argv[1]
    ctx, body = assemble(open(src).read())
    msg = message(ctx, body)
    if "--hex" in sys.argv:
        print(msg.hex())
    if "-o" in sys.argv:
        with open(sys.argv[sys.argv.index("-o") + 1], "wb") as f:
            f.write(msg)


if __name__ == "__main__":
    main()
