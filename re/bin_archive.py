"""Format hypothesis for data/*.bin: u32 n, then n increasing u32 offsets (< file size).

Usage : python -I re/bin_archive.py "D:/Games/Silent Hill 4/data"
Prints the files that violate the hypothesis and the distribution of the first offset.
"""
import collections, pathlib, struct, sys


def parse(head, size):
    if len(head) < 4:
        return None
    n = struct.unpack_from("<I", head, 0)[0]
    if not 0 < n < 4096 or 4 + 4 * n > len(head):
        return None
    offs = list(struct.unpack_from(f"<{n}I", head, 4))
    if any(b <= a for a, b in zip(offs, offs[1:])) or offs[-1] >= size or offs[0] < 4 + 4 * n:
        return None
    return offs


def demo():
    blob = struct.pack("<3I", 2, 0x80, 0x100)
    assert parse(blob, 0x200) == [0x80, 0x100]
    assert parse(struct.pack("<3I", 2, 0x100, 0x80), 0x200) is None


if __name__ == "__main__":
    demo()
    first, bad = collections.Counter(), []
    files = sorted(pathlib.Path(sys.argv[1]).glob("*.bin"))
    for f in files:
        with open(f, "rb") as h:
            head = h.read(64 * 1024)
        offs = parse(head, f.stat().st_size)
        if offs:
            first[offs[0]] += 1
        else:
            bad.append((f.name, f.stat().st_size, head[:16].hex(" ")))
    print(f"{len(files) - len(bad)}/{len(files)} conforming; first offset : {dict(first.most_common(8))}")
    for name, size, h in bad[:40]:
        print(f"  NON CONFORME {name:24} {size:>10} {h}")
