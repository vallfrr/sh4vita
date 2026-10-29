"""Extract existing low-overhead perf windows from a stationary audit benchmark."""
import argparse
import json
from pathlib import Path
import re


def windows(text, first, last, captures=()):
    result, current = [], {}
    previous = None
    for line in text.splitlines():
        match = re.match(r"\[perf\] ([\d.]+) ms/frame; frame body ([\d.]+) ms", line)
        if match:
            current = dict(mean_ms=float(match[1]), body_ms=float(match[2]))
        match = re.match(r"\[perf\] frames > 36 ms: (\d+) of (\d+), longest ([\d.]+) ms", line)
        if match:
            current.update(over_36ms=int(match[1]), frames=int(match[2]), max_ms=float(match[3]))
        match = re.match(r"\[q\] interpolation: (\d+) intermediate frames, .*?intermediate pass ([\d.]+) ms, pairing ([\d.]+) ms .*?, real ([\d.]+) ms", line)
        if match and "mean_ms" in current:
            current.update(intermediate_frames=int(match[1]), intermediate_submit_ms=float(match[2]),
                           pairing_ms=float(match[3]), real_submit_ms=float(match[4]))
        match = re.match(r"\[stats\] presents=(\d+)", line)
        if match:
            end = int(match[1])
            begin = previous + 1 if previous is not None else end
            if current and previous is not None and begin >= first and end <= last and not any(begin <= f + 3 and end >= f - 3 for f in captures):
                result.append(dict(first_frame=begin, last_frame=end, **current))
            previous, current = end, {}
    return result


def benchmark(folder):
    text = (folder / "sh4log.txt").read_text(encoding="utf-8", errors="replace")
    config = json.loads((folder / "config.json").read_text(encoding="utf-8"))
    arrivals = re.findall(r"\[audit\] event=arrive index=\d+ frame=(\d+)", text)
    if len(arrivals) != 1:
        raise ValueError("Stationary benchmark requires exactly one sustained arrival")
    first = int(arrivals[0]) + int(config["SH4_AUDIT_SETTLE"])
    last = first + int(config["SH4_AUDIT_DWELL"]) - 1
    captures = [int(f) for f in re.findall(r"\[audit\] event=capture index=\d+ frame=(\d+)", text)]
    measured = windows(text, first, last, captures)
    count = sum(w["frames"] for w in measured)
    interpolation_measured = bool(measured) and all("intermediate_frames" in w for w in measured)
    intermediate = sum(w["intermediate_frames"] for w in measured) if interpolation_measured else None
    return dict(config=config, windows=measured, frames=count,
                weighted_mean_ms=sum(w["mean_ms"] * w["frames"] for w in measured) / count if count else None,
                intermediate_frames=intermediate, interpolation_ratio=intermediate / count if interpolation_measured and count else None,
                max_ms=max((w["max_ms"] for w in measured), default=None),
                limit="Window means are rounded to 0.1 ms; no individual-frame percentile is available. Interpolation uses render-window counters, not physical display timestamps.")


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("folder", type=Path)
    ap.add_argument("--out", type=Path)
    args = ap.parse_args()
    result = benchmark(args.folder)
    if args.out:
        args.out.write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(f"{result['frames']} frames, mean {result['weighted_mean_ms']} ms; {len(result['windows'])} whole windows excluding captures")
