"""Research the immutable model descriptors in locally owned GOG v5 data.

This is an audit, not a replacement asset format. It emits counts and hashes,
never source geometry. With --log it checks descriptors observed by a temporary
renderer probe against their original files. Run only on the exact supported
GOG data and keep runtime logs outside Git.
"""

import argparse
import hashlib
import re
import struct
from collections import Counter, defaultdict
from pathlib import Path


def u32(data: bytes, at: int) -> int:
    return struct.unpack_from("<I", data, at)[0]


def scan_file(path: Path):
    data = path.read_bytes()
    size = len(data)
    if size < 260:
        return []
    count = u32(data, 0)
    if not (0 < count < 100000 and 4 + 4 * count <= size):
        return []
    offsets = [u32(data, 4 + 4 * i) for i in range(count)]
    if offsets != sorted(offsets) or offsets[-1] > size:
        return []
    hits = []
    for start, end in zip(offsets, offsets[1:] + [size]):
        if start >= end:
            continue
        for at in range(start, max(start, end - 256), 2):
            rel = u32(data, at + 8)
            if rel < 4 or rel > end - at - 100:
                continue
            streams = [(u32(data, at + c), u32(data, at + o), width)
                       for c, o, width in ((0x14, 0x18, 6), (0x1c, 0x20, 2), (0x24, 0x28, 2))]
            if any(n > 10000 or (n and (off >= end - at or n * width > end - at - off))
                   for n, off, width in streams):
                continue
            header = at + rel
            groups = u32(data, header)
            table = u32(data, header + 8)
            if not (1 <= groups <= 100 and 12 <= table <= 0x10000):
                continue
            first = header + table
            if first + groups * 0x5C > end:
                continue
            signatures = []
            for i in range(groups):
                group = first + i * 0x5C
                indices, vertices, stride = (u32(data, group + k) for k in (0x40, 0x44, 0x48))
                index_rel, vertex_rel = u32(data, group + 0x50), u32(data, group + 0x54)
                if not (0 < indices < 1000000 and 0 < vertices < 1000000 and stride == 64):
                    break
                if not (group + index_rel + indices * 2 <= end and
                        group + vertex_rel + 0x20 + vertices * 64 <= end):
                    break
                source = group + vertex_rel + 0x20
                signatures.append((vertices, indices, index_rel, vertex_rel,
                                   u32(data, source), u32(data, source + 4)))
            if len(signatures) == groups:
                hits.append((at, streams[0][0], signatures))
    return hits


LOG_ROW = re.compile(
    r"\[model-desc\] frame=\d+ guest=[0-9a-f]+ count=\d+ group=[0-9a-f]+"
    r" head=[0-9a-f]+ up=(\d+) i1=\d+ i2=\d+ mode=\d+"
    r" vc=(\d+) ic=(\d+) stride=(\d+) data_i=([0-9a-f]+)"
    r" data_v=([0-9a-f]+) v0=([0-9a-f]+) ([0-9a-f]+)"
)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("data", type=Path, help="data/ directory from the supported GOG PC installation")
    ap.add_argument("files", nargs="*", help=".bin file names to inspect; omitted with --log")
    ap.add_argument("--log", type=Path, help="private PC log containing [model-desc] probe records")
    args = ap.parse_args()
    log = args.log.read_text(errors="replace") if args.log else ""
    names = args.files or sorted(set(re.findall(r'CreateFileA\("\.\\data\\([^"\\]+\.bin)"\)', log)))
    if not names:
        ap.error("provide .bin file names or --log")
    lookup = defaultdict(list)
    for name in names:
        path = args.data / name
        if not path.is_file():
            continue
        hits = scan_file(path)
        for at, updates, groups in hits:
            for signature in groups:
                lookup[signature].append((name, at, updates))
        print(f"{name}: sha256={hashlib.sha256(path.read_bytes()).hexdigest()} "
              f"candidates={len(hits)} groups={sum(len(groups) for _, _, groups in hits)}")
    if args.log:
        total = unique = ambiguous = animated = 0
        files = Counter()
        for m in LOG_ROW.finditer(log):
            updates, vertices, indices, stride, index_rel, vertex_rel, v0, v1 = m.groups()
            signature = (int(vertices), int(indices), int(index_rel, 16), int(vertex_rel, 16),
                         int(v0, 16), int(v1, 16))
            matches = lookup[signature]
            total += 1
            unique += len(matches) == 1
            ambiguous += len(matches) > 1
            animated += int(updates) > 0
            if matches:
                files[matches[0][0]] += 1
        print(f"runtime={total} unique={unique} duplicate={ambiguous} unmatched={total-unique-ambiguous} "
              f"with_position_updates={animated} files={dict(files)}")


if __name__ == "__main__":
    main()
