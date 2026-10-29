"""Build a local, research-only model metadata sidecar from GOG data.

The game and sh4prep do not consume this format. Never add the output to Git.
"""

import argparse
import hashlib
import struct
from pathlib import Path

from model_data_audit import scan_file


MAGIC = b"SH4MDL01"
SCHEMA = 1


def build(data_dir: Path, names: list[str]) -> tuple[bytes, int, int]:
    out = bytearray(MAGIC + struct.pack("<II", SCHEMA, len(names)))
    descriptors = groups = 0
    for name in names:
        if Path(name).name != name or not name.lower().endswith(".bin"):
            raise ValueError(f"expected a .bin file name: {name}")
        source_path = data_dir / name
        source = source_path.read_bytes()
        found = scan_file(source_path)
        encoded = name.encode("ascii")
        out += struct.pack("<HQ", len(encoded), len(source))
        out += encoded + hashlib.sha256(source).digest()
        out += struct.pack("<I", len(found))
        for at, updates, signatures in found:
            out += struct.pack("<IIH", at, updates, len(signatures))
            for signature in signatures:
                out += struct.pack("<6I", *signature)
            groups += len(signatures)
        descriptors += len(found)
    return bytes(out), descriptors, groups


def verify(index: bytes, data_dir: Path) -> tuple[int, int]:
    if len(index) < 16 or index[:8] != MAGIC:
        raise ValueError("wrong model index magic")
    schema, count = struct.unpack_from("<II", index, 8)
    if schema != SCHEMA:
        raise ValueError(f"unsupported model index schema {schema}")
    cursor = 16
    descriptors = groups = 0
    for _ in range(count):
        if cursor + 10 > len(index):
            raise ValueError("truncated source record")
        name_len, source_len = struct.unpack_from("<HQ", index, cursor)
        cursor += 10
        if cursor + name_len + 36 > len(index):
            raise ValueError("truncated source identity")
        name = index[cursor:cursor + name_len].decode("ascii")
        cursor += name_len
        if Path(name).name != name or not name.lower().endswith(".bin"):
            raise ValueError("invalid source file name")
        digest = index[cursor:cursor + 32]
        cursor += 32
        source = (data_dir / name).read_bytes()
        if len(source) != source_len or hashlib.sha256(source).digest() != digest:
            raise ValueError(f"source changed: {name}")
        n, = struct.unpack_from("<I", index, cursor)
        cursor += 4
        for _ in range(n):
            if cursor + 10 > len(index):
                raise ValueError("truncated descriptor")
            at, updates, ng = struct.unpack_from("<IIH", index, cursor)
            cursor += 10
            if at >= source_len or not ng or cursor + ng * 24 > len(index):
                raise ValueError("invalid descriptor bounds")
            cursor += ng * 24
            groups += ng
        descriptors += n
    if cursor != len(index):
        raise ValueError("unexpected trailing model index data")
    return descriptors, groups


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("data", type=Path, help="data/ directory of the owned GOG PC installation")
    ap.add_argument("output", type=Path, help="private output under an ignored build/ directory")
    ap.add_argument("files", nargs="+", help=".bin file names to index")
    args = ap.parse_args()
    index, descriptors, groups = build(args.data, args.files)
    assert verify(index, args.data) == (descriptors, groups)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(index)
    print(f"bytes={len(index)} sources={len(args.files)} descriptors={descriptors} groups={groups} "
          f"sha256={hashlib.sha256(index).hexdigest()}")


if __name__ == "__main__":
    main()
