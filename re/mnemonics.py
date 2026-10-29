"""Inventory of x86 mnemonics in re/out/asm/*.asm (REA export read_function_instructions).

Usage : python -I re/mnemonics.py re/out/asm [--prefix F]
"""
import collections, pathlib, re, sys

LINE = re.compile(r"^0x[0-9a-f]+: (?:(?:LOCK|REP|REPE|REPNE|REPZ|REPNZ)\s+)?([A-Z][A-Z0-9.]*)")


def histogram(asm_dir):
    count, funcs = collections.Counter(), collections.defaultdict(set)
    for f in pathlib.Path(asm_dir).glob("*.asm"):
        for line in f.read_text(encoding="utf-8").splitlines():
            m = LINE.match(line)
            if m:
                count[m.group(1)] += 1
                funcs[m.group(1)].add(f.stem)
    return count, funcs


def demo():
    assert LINE.match("0x5696e0: XOR EAX, EAX").group(1) == "XOR"
    assert LINE.match("0x401000: REP MOVSD.REP ES:EDI, ESI").group(1) == "MOVSD.REP"


if __name__ == "__main__":
    demo()
    count, funcs = histogram(sys.argv[1])
    prefix = sys.argv[3] if len(sys.argv) > 3 and sys.argv[2] == "--prefix" else ""
    total = sum(count.values())
    print(f"{total} instructions, {len(count)} distinct mnemonics")
    for m, n in count.most_common():
        if m.startswith(prefix):
            print(f"{m:14} {n:8} {100 * n / total:6.2f}%  {len(funcs[m]):5} functions")
