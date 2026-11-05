#!/usr/bin/env python3
"""Generate structured seed inputs for fuzz_core (see fuzz_core.c opcodes)."""
import os, random, struct, sys

out = sys.argv[1] if len(sys.argv) > 1 else "seeds_core"
os.makedirs(out, exist_ok=True)

def op(code, conn, payload=b""):
    assert len(payload) < 64
    return bytes([code, conn, len(payload)]) + payload

def s8(t): return bytes([len(t)]) + t
def reg(dev, name=b"p", key=b"k"): return b"\x10\x02\x06" + bytes([0,0,0,0,0,dev]) + b"\x00" + s8(name) + s8(key)
def host(uid=0x51, flags=0, maxp=4): return b"\x20" + struct.pack(">I", uid) + bytes([2, maxp, flags]) + s8(b"h")
def join(sid=0, uid=0x51, rejoin=0): return b"\x32" + struct.pack(">II", sid, uid) + bytes([2, rejoin])
def data(dst, txt=b"x"): return b"\x40" + bytes([dst, 0]) + txt
def msg(conn, m): return op(5, conn, m)
def connect(c, addr=0): return op(0 | (addr << 6), c)
def hello(c, lid): return op(4 | ((lid - 1) << 4), c)
def tick(n=1): return op(2 | (n << 4), 0)

base = b""
for c in range(3):
    base += connect(c, c % 2) + hello(c, c + 1) + msg(c, reg(c + 1))
base += msg(0, host()) + msg(1, join()) + msg(2, join())
seeds = {
    "basic": base + msg(0, data(0xFF)) + msg(1, data(0)) + tick(3),
    "rehello": base + hello(0, 2) + msg(2, join()) + tick(2),
    "close_hello": base + op(9, 0, b"\x07\x00\x00\x05") + hello(0, 2) + tick(1),
    "stall": base + b"".join(msg(0, data(1)) for _ in range(30)) + msg(0, b"\x22\x00"),
    "kick": base + b"".join(msg(0, data(1)) for _ in range(30)) + msg(0, b"\x28\x01\x02"),
    "confirm": base.replace(host(), host(flags=1)) + msg(0, b"\x25\x01\x01") + tick(9),
    "reserve": base + msg(0, b"\x23\x01") + op(1, 1) + tick(15) + connect(1) + hello(1, 2) + tick(2),
    "badhello": base + op(9, 0, b"\x01\x00\x00\x00\x00") + op(1, 0) + msg(2, join()),
    "discover": base + msg(2, b"\x30\x00\x00\x00\x51\x02"),
}
for name, s in seeds.items():
    open(os.path.join(out, name), "wb").write(s)
print("wrote", len(seeds), "seeds to", out)
