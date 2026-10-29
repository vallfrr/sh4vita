"""Summarize actual Vita file stalls before adding derived data to sh4prep.

The runtime log caps detailed I/O records at 400 per process. This tool ranks
those observed records; it does not estimate unlogged reads or steady FPS.
"""

import argparse
from collections import defaultdict
import json
from pathlib import Path
import re


OPEN = re.compile(r'CreateFileA\("([^"]+)"\) -> ([0-9a-fA-F]+)')
READ = re.compile(
    r'\[io\] frame (\d+) h=([0-9a-fA-F]+) pos=(-?\d+) n=(\d+) '
    r'KB=\d+ ([\d.]+) ms \([\d.]+ MB/s\) thread=(\d+) ahead=(\d+)'
)


def read_log(path: Path) -> list[dict]:
    handles = {}
    reads = []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if match := OPEN.search(line):
            handles[int(match[2], 16)] = match[1].replace("\\", "/")
        elif match := READ.search(line):
            frame, handle, offset, size, ms, thread, ahead = match.groups()
            handle = int(handle, 16)
            reads.append(dict(
                log=str(path), file=handles.get(handle, f"unknown-handle-{handle:x}"),
                frame=int(frame), offset=int(offset), bytes=int(size), ms=float(ms),
                main_thread=thread == "1", read_ahead=ahead == "1",
            ))
    return reads


def summarize(reads: list[dict]) -> list[dict]:
    groups = defaultdict(list)
    for read in reads:
        groups[read["file"]].append(read)
    result = []
    for file, records in groups.items():
        foreground = [r for r in records if r["main_thread"] and not r["read_ahead"]]
        ranges = defaultdict(int)
        for record in foreground:
            # Repeated visits across separate process launches are not a cache
            # opportunity within one play session.
            ranges[(record["log"], record["offset"], record["bytes"])] += 1
        result.append(dict(
            file=file, observed_reads=len(records), foreground_reads=len(foreground),
            foreground_ms=round(sum(r["ms"] for r in foreground), 1),
            worst_foreground_ms=round(max((r["ms"] for r in foreground), default=0), 1),
            repeated_ranges=sum(n - 1 for n in ranges.values()),
            examples=sorted(foreground, key=lambda r: r["ms"], reverse=True)[:3],
        ))
    return sorted(result, key=lambda r: r["foreground_ms"], reverse=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("logs", nargs="+", type=Path)
    parser.add_argument("--json", type=Path, help="write individual reads and per-file totals")
    args = parser.parse_args()
    reads = [read for path in args.logs for read in read_log(path)]
    summary = summarize(reads)
    for entry in summary[:20]:
        print(f"{entry['foreground_ms']:8.1f} ms  peak {entry['worst_foreground_ms']:6.1f}  "
              f"reads {entry['foreground_reads']:3d} repeated {entry['repeated_ranges']:3d}  {entry['file']}")
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps({"summary": summary, "reads": reads}, indent=2), encoding="utf-8")
    print(f"{len(reads)} logged large reads across {len(args.logs)} logs; logs may be capped")


if __name__ == "__main__":
    main()
