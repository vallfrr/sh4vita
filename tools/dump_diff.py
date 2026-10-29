#!/usr/bin/env python3
"""Offline diff of guest-memory dumps (ctl dump=N -> ux0:data/sh4/gdump_N.bin, base 0x5c0000).

dump_diff.py --base 0x5c0000 --a gdump_0.bin gdump_1.bin --b gdump_2.bin gdump_3.bin [--c gdump_4.bin ...]

Lists the dwords that are constant inside group A, constant inside group B and different between
A and B (a state flag), keeping only those that group C (other states that must NOT count, for
example the Start menu) leaves equal to A. Values only: a flag whose value differs is found even
if it is non-zero in both states.
"""
import argparse
import struct
import sys


def load(path):
    with open(path, "rb") as f:
        data = f.read()
    return memoryview(data[: len(data) // 4 * 4]).cast("I")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--base", default="0x5c0000")
    ap.add_argument("--a", nargs="+", required=True)
    ap.add_argument("--b", nargs="+", required=True)
    ap.add_argument("--c", nargs="*", default=[])
    ap.add_argument("--limit", type=int, default=200)
    ap.add_argument("--small", action="store_true", help="keep only values below 0x10000 in both states (flags/counters, not pointers)")
    args = ap.parse_args()
    base = int(args.base, 0)
    A = [load(p) for p in args.a]
    B = [load(p) for p in args.b]
    C = [load(p) for p in args.c]
    n = min(len(x) for x in A + B + C)
    out = []
    for i in range(n):
        a0 = A[0][i]
        b0 = B[0][i]
        if a0 == b0:
            continue
        if any(x[i] != a0 for x in A[1:]) or any(x[i] != b0 for x in B[1:]):
            continue
        if args.small and (a0 > 0xFFFF or b0 > 0xFFFF):
            continue
        if any(x[i] != a0 for x in C):
            continue
        out.append((base + i * 4, a0, b0))
    print(f"{len(out)} candidate dwords (A!=B, stable inside A and B" + (", equal to A in C" if C else "") + ")")
    for addr, a0, b0 in out[: args.limit]:
        print(f"0x{addr:08x}  A=0x{a0:08x}  B=0x{b0:08x}")
    if len(out) > args.limit:
        print(f"... {len(out) - args.limit} more")


if __name__ == "__main__":
    sys.exit(main())
