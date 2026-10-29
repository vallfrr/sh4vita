"""Local room inventory and audit reports; never copies game assets into the repo."""
import argparse
import csv
import hashlib
import gzip
import json
import math
from pathlib import Path
import re
import statistics
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "recomp"))
from pe import PE

WORLDS = {1: ("apartment", "rl"), 2: ("subway", "sb"), 3: ("forest", "fr"),
          4: ("water", "mz"), 5: ("building", "bd"), 6: ("home", "ie"),
          7: ("hospital", "hs"), 8: ("past", "cm"), 9: ("last", "ls"),
          10: ("tunnels", "gt"), 11: ("spiral", "st")}
APARTMENT_SELECTORS = (0x62fdf0, 0x62fe40, 0x62fe68, 0x62fea0, 0x62feb8, 0x62fec8, 0x62fee0)


def resource_name(pe, resource):
    if resource & 0xf000f000 != 0xf000f000:
        return None
    pointer = pe.u32(0x69f698 + (resource & 0xfff) * 12)
    return pe.cstr(pointer) if pointer and pe.off(pointer) is not None else None


def preloads(pe, world, scene, visit):
    table = pe.u32((0x62d548 if visit else 0x62d514) + world * 4)
    pointer = pe.u32(table + scene * 4) if table else 0
    result = []
    for i in range(128):
        resource = pe.u32(pointer + i * 4) if pointer else 0xfffffff
        if resource == 0xfffffff:
            return result
        name = resource_name(pe, resource)
        if not name:
            raise ValueError(f"Bad preload list {world}:{scene}:{visit}")
        result.append(name)
    raise ValueError("Unterminated preload list")


def catalog(exe, data):
    pe = PE(exe)
    digest = hashlib.sha256(pe.data).hexdigest()
    if digest != "cf7081a6c78dbe51f6dc2e1c9bd978d8486f1d7b59d7a3b88e027c9b39f21dd0":
        raise ValueError("Only the documented GOG v5 executable is supported")
    rooms, assets, cases = [], [], []
    for world, (name, prefix) in WORLDS.items():
        table = pe.u32(0x62fffc + world * 4)
        maximum = 7 if world == 1 else {9: 2, 10: 11, 11: 7}.get(world, pe.u32(table) if table else 0)
        backgrounds = pe.u32(0x62fdb0 + world * 4)
        if not 0 < maximum < 256 or not backgrounds:
            raise ValueError(f"Invalid room/background table for world {world}")
        for scene in range(1, maximum + 1):
                blob = pe.u32(table + (scene + 3) * 4) if table and scene <= pe.u32(table) else 0
                header = pe.read(blob, 4) if blob else None
                entries = []
                for i in range(header[0] if header else 0):
                    r = pe.read(blob + 4 + i * 0x38, 0x38)
                    if not r or len(r) != 0x38 or r[13] != scene:
                        raise ValueError(f"Invalid door record {world}:{scene}:{i}")
                    xyz = struct.unpack_from("<3f", r, 0x20)
                    direction = struct.unpack_from("<3f", r, 0x2c)
                    if not all(math.isfinite(x) for x in xyz + direction):
                        raise ValueError("Nonfinite spawn")
                    entries.append(dict(door=r[12], position=xyz,
                                        rotation=math.atan2(direction[0], direction[2])))
                variants = []
                if world == 1:
                    pointer = APARTMENT_SELECTORS[scene - 1]
                    for i in range(32):
                        bg, flag = pe.u32(pointer + i * 8), pe.u32(pointer + i * 8 + 4)
                        asset = resource_name(pe, pe.u32(backgrounds + (bg - 1) * 28 + 24))
                        if not asset:
                            raise ValueError("Invalid apartment background")
                        variants.append(dict(bg=bg, asset=asset, condition_flag=flag))
                        if flag == 0:
                            break
                    asset = variants[-1]["asset"]
                else:
                    asset = resource_name(pe, pe.u32(backgrounds + (scene - 1) * 28 + 24))
                    if world == 10 and scene == 1:
                        variants = [dict(bg=1, asset=asset), dict(bg=12, asset=resource_name(pe, pe.u32(backgrounds + 11 * 28 + 24)))]
                loads = [preloads(pe, world, scene, v) for v in (0, 1)]
                if not asset:
                    raise ValueError(f"Missing background descriptor for logical room {world}:{scene}")
                rooms.append(dict(world=world, scene=scene, name=name, asset=asset,
                                  asset_exists=bool(asset and (data / asset).is_file()), entries=entries,
                                  source="exe-background/preload/door-tables", eligible=not (world == 8 and scene == 22),
                                  reserved_alias=world == 8 and scene == 22,
                                  backgrounds=variants, preloads=loads,
                                  world_preloads=[preloads(pe, world, 0, v) for v in (0, 1)]))
        known = {r["asset"] for r in rooms if r["world"] == world}
        known.update(v["asset"] for r in rooms if r["world"] == world for v in r["backgrounds"])
        for asset in sorted(data.glob(f"{prefix}[0-9][0-9].bin")):
            if asset.name not in known:
                references = [(r["world"], r["scene"], v) for r in rooms for v in (0, 1) if asset.name in r["preloads"][v]]
                assets.append(dict(world=world, asset=asset.name, preload_references=references,
                                   reason="additional-resource-inside-room" if references else "unmapped-resource"))
    # One apartment descriptor is absent from the selector lists. Keep the inferred
    # logical room and source explicit rather than silently dropping this asset.
    apartment = [r for r in rooms if r["world"] == 1]
    selected_bg = {v["bg"] for r in apartment for v in r["backgrounds"]}
    for bg in range(1, 31):
        if bg in selected_bg:
            continue
        table = pe.u32(0x62fdb0 + 4)
        asset = resource_name(pe, pe.u32(table + (bg - 1) * 28 + 24))
        logical = int(asset[2:4]) + 1
        next(r for r in apartment if r["scene"] == logical)["backgrounds"].append(
            dict(bg=bg, asset=asset, condition_flag=None, source="descriptor-only; logical room inferred from asset name"))
    for r in rooms:
        if not r["eligible"]:
            continue
        base = dict(world=r["world"], scene=r["scene"], entry=r["entries"][0]["door"] if r["entries"] else 0,
                    visit=0, bg=0, kind="base")
        cases.append(base)
        if r["preloads"][0] != r["preloads"][1] or r["world_preloads"][0] != r["world_preloads"][1]:
            cases.append(dict(base, visit=1, kind="revisit-resource-set"))
        for variant in {v["bg"]: v for v in r["backgrounds"]}.values():
            cases.append(dict(base, bg=variant["bg"], asset=variant["asset"], kind="forced-background"))
    required_assets = {r["asset"] for r in rooms if r["eligible"]}
    required_assets.update(v["asset"] for r in rooms for v in r["backgrounds"])
    required_assets.update(asset for r in rooms for group in (r["preloads"], r["world_preloads"]) for visit in group for asset in visit)
    return dict(schema=2, exe_sha256=digest, rooms=rooms, cases=cases, extra_assets=assets,
                missing_assets=sorted(asset for asset in required_assets if not (data / asset).is_file()),
                limits=["Door tables are not a list of story variants or cutscenes.",
                        "A warp does not reconstruct the normal progression flags.",
                        "A loaded room is not automatically a visually verified room."])


def percentile(values, p):
    return sorted(values)[max(0, math.ceil(len(values) * p) - 1)] if values else None


def valid_bmp(data):
    if len(data) < 54 or data[:2] != b"BM":
        return False
    size, offset = struct.unpack_from("<I", data, 2)[0], struct.unpack_from("<I", data, 10)[0]
    dib, width, height, planes, depth, compression = struct.unpack_from("<IiiHHI", data, 14)
    if dib < 40 or offset < 14 + dib or width <= 0 or not height or planes != 1 or depth not in (24, 32) or compression not in (0, 3):
        return False
    return size == len(data) == offset + ((width * depth + 31) // 32 * 4) * abs(height)


def report(census, logs):
    runs, alerts = {}, []
    logs = [file for log in logs for file in (sorted(log.rglob("sh4log.txt")) if log.is_dir() else [log])]
    for log in logs:
        text = log.read_text(encoding="utf-8", errors="replace")
        config_path = log.parent / "config.json"
        log_config = json.loads(config_path.read_text(encoding="utf-8")) if config_path.is_file() else {}
        long_frames = {int(frame): dict(duration_ms=float(ms), shader_link_ms=float(link)) for frame, ms, link in
                       re.findall(r"\[long\] frame (\d+): ([\d.]+) ms \(body [\d.]+ of which GL [\d.]+ links ([\d.]+)", text)}
        completed_captures = set(re.findall(r"\[(?:shot|dump)\] ([^\r\n]+)", text))
        render = {}
        for line in text.splitlines():
            if any(tag in line for tag in ("[audit-render] ", "[audit-swap] ", "[audit-pass] ")):
                fields = dict(re.findall(r"(\w+)=([^\s]+)", line))
                if "frame" in fields:
                    render.setdefault(int(fields["frame"]), {}).update(fields)
        capture_active = False
        capture_path = None
        last_frame_fields = {}
        for line in text.splitlines():
            if "[audit] " in line:
                fields = dict(re.findall(r"(\w+)=([^\s]+)", line.split("[audit] ", 1)[1]))
                if "world" not in fields or "scene" not in fields:
                    continue
                key = (str(log), int(fields["world"]), int(fields["scene"]), int(fields.get("index", 0)))
                room = runs.setdefault(key, dict(log=str(log), world=key[1], scene=key[2],
                                                samples=[], captures=[], events=[]))
                event = fields.get("event")
                if event == "frame":
                    last_frame_fields = fields
                    fields.update({k: v for k, v in render.get(int(fields["frame"]), {}).items() if k != "frame"})
                    room["samples"].append(fields)
                elif event == "capture":
                    capture_active = True
                    capture_path = fields.get("path")
                    room["captures"].append(fields)
                else:
                    room["events"].append(fields)
            unsupported = re.search(r"\[d3d8\].*(?:unsupported|ignored|skipped|CreateVolumeTexture|CreateCubeTexture|CreateAdditionalSwapChain|UpdateTexture|GetFrontBuffer|ProcessVertices|GetVertexShaderDeclaration|GetVertexShaderFunction|DrawRectPatch|DrawTriPatch)", line)
            if unsupported or re.search(r"FATAL|\[stall\]|compile failure|link failure|write failure|not enough memory|\[uniform-location\]|\[audit-error\]|\[audit-format\]|\[shot-error\]|exhausted|failed|traduction partielle|WARN:|unhandled|unknown GUID", line, re.I):
                expected = 'save\\sh4.sav") failed' in line
                capture_stall = capture_active and capture_path in completed_captures and ("[stall]" in line or "no Present for" in line)
                pc_copy_limit = "SH4_HEADLESS" in log_config and line == "[d3d8] CopyRects: destination too large, copy skipped"
                pc_no_pad = "SH4_HEADLESS" in log_config and line == "[dinput] CreateDevice: unknown GUID 00000000"
                alert = dict(log=str(log), line=line, category="expected-empty-test-save" if expected else
                                   "capture-overhead" if capture_stall else "known-pc-copy-limit" if pc_copy_limit else
                                   "pc-no-controller" if pc_no_pad else "investigate")
                stalled = re.search(r"\[audit-error\] frame=(\d+) no Present", line)
                if stalled:
                    frame = int(stalled[1])
                    timing = long_frames.get(frame)
                    if timing:
                        alert.update(timing)
                        if timing["shader_link_ms"] >= timing["duration_ms"] * .8:
                            alert["cause"] = "shader-program-preparation"
                            alert["category"] = "investigate"
                    if int(last_frame_fields.get("frame", -1)) == frame:
                        alert["context"] = {k: last_frame_fields[k] for k in ("phase", "actual", "control_blocked") if k in last_frame_fields}
                alerts.append(alert)
            if "[shot] " in line or "[shot-error] " in line or "[dump] " in line:
                capture_active = False
    results = []
    for room in runs.values():
        samples = room.pop("samples")
        steady = [s for s in samples if s.get("phase") == "measure" and s.get("capture") == "0"
                  and s.get("actual") == f'{room["world"]}:{room["scene"]}']
        ms = [float(s["ms"]) for s in steady]
        noncapture = [s for s in samples if s.get("capture") == "0"]
        matching = [s for s in noncapture if s.get("actual") == f'{room["world"]}:{room["scene"]}']
        state_windows = {}
        for state, field in (("movie", "movie"), ("control_blocked", "control_blocked"), ("scripted", "script")):
            selected = [s for s in matching if field in s and int(s[field]) != 0]
            intervals = [float(s["ms"]) for s in selected]
            worst = max(selected, key=lambda s: float(s["ms"]), default=None)
            state_windows[state] = dict(observed=any(field in s for s in matching), frames=len(selected),
                                       mean_ms=statistics.mean(intervals) if intervals else None,
                                       p95_ms=percentile(intervals, .95), max_ms=max(intervals, default=None),
                                       over_100ms=sum(x > 100 for x in intervals),
                                       worst_frame=dict(worst) if worst else None)
        capture_files = []
        for c in room["captures"]:
            name = c.get("path", "").replace("\\", "/").rsplit("/", 1)[-1]
            path = Path(room["log"]).parent / name
            if not path.is_file():
                path = Path(c.get("path", ""))
            if path.is_file():
                data = path.read_bytes()
                try:
                    pixels = gzip.decompress(data) if path.suffix == ".gz" else data
                except (OSError, EOFError):
                    pixels = b""
                valid = ((path.name.endswith((".tga", ".tga.gz"))) and len(pixels) == 18 + 960 * 544 * 4
                         and pixels[2] == 2 and struct.unpack_from("<HHB", pixels, 12) == (960, 544, 32)) or (
                    path.suffix == ".bmp" and valid_bmp(data))
                sample_max = None
                if valid:
                    offset = 18 if path.name.endswith((".tga", ".tga.gz")) else struct.unpack_from("<I", pixels, 10)[0]
                    stride = 4 if offset == 18 else struct.unpack_from("<H", pixels, 28)[0] // 8
                    sample_max = max(max(pixels[offset + channel::stride * 32], default=0) for channel in range(3))
                capture_files.append(dict(path=str(path), valid=valid, sha256=hashlib.sha256(data).hexdigest(),
                                          sampled_rgb_max=sample_max, needs_dark_image_review=valid and sample_max <= 3))
        room.update(measured_frames=len(ms), mean_ms=statistics.mean(ms) if ms else None,
                    all_phase_peak_ms=max((float(s["ms"]) for s in noncapture), default=None),
                    loading_peak_ms=max((float(s["ms"]) for s in noncapture if s.get("phase") == "load"), default=None),
                    shader_link_peak_ms=max((float(s["link"]) for s in noncapture if "link" in s), default=None),
                    state_windows=state_windows,
                    p95_ms=percentile(ms, .95), p99_ms=percentile(ms, .99),
                    max_ms=max(ms) if ms else None,
                    over_36ms=sum(x > 36 for x in ms), over_100ms=sum(x > 100 for x in ms),
                    completed=any(e.get("event") == "complete" for e in room["events"]),
                    capture_files=capture_files,
                    draw_mean=statistics.mean(int(s.get("draws", 0)) for s in steady) if steady else None,
                    draw_max=max((int(s.get("draws", 0)) for s in steady), default=None),
                    components={k: statistics.mean(float(s.get(k, 0)) for s in steady) for k in
                                ("body", "gl", "io", "wait", "sleep", "link", "tex")} if steady else {},
                    heap_peak_kb=max((int(s.get("heap_kb", 0)) for s in samples), default=0),
                    backgrounds_observed=sorted({int(s["bg"]) for s in steady if "bg" in s}),
                    requested_case=next((e for e in room["events"] if e.get("event") == "request"), {}),
                    visual_status="unreviewed")
        config_path = Path(room["log"]).parent / "config.json"
        config = json.loads(config_path.read_text(encoding="utf-8")) if config_path.exists() else {}
        room["platform"] = "vita" if config.get("SH4_AUDIT_TOUR", "").startswith("ux0:") else "pc" if config.get("SH4_HEADLESS") else "unknown"
        room["capture_stage"] = "final-display" if room["platform"] == "vita" or "SH4_AUDIT_DISPLAY" in config else "raw-game-backbuffer"
        room["virtual_time"] = config.get("SH4_VTIME") == "1"
        room["gl_timers_enabled"] = "SH4_PH" in config
        room["validation_enabled"] = config.get("SH4_AUDIT_VALIDATE") != "0"
        binary_path = Path(room["log"]).parent.parent / "binary.sha256"
        room["binary_sha256"] = binary_path.read_text(encoding="ascii").strip() if binary_path.is_file() else None
        room["movie_state_logged"] = any("movie" in sample for sample in samples)
        real = [int(s["real_us"]) / 1000 for s in steady if "real_us" in s]
        gl_checks = [s for s in steady if "gl_error" in s] if room["validation_enabled"] else []
        gxm_checks = [s for s in steady if s.get("gxm_error_checked") == "1"] if room["validation_enabled"] else []
        room["render_pass"] = dict(samples=len(real), mean_ms=statistics.mean(real) if real else None,
                                   p95_ms=percentile(real, .95), intermediate_frames=sum(int(s.get("intermediate", 0)) for s in steady),
                                   gl_error_checks=len(gl_checks), gl_errors=sum(int(s["gl_error"]) != 0 for s in gl_checks) if gl_checks else None,
                                   gxm_error_checks=len(gxm_checks), gxm_errors=sum(int(s["gxm_error"]) != 0 for s in gxm_checks) if gxm_checks else None,
                                   gpu_finish_measured="SH4_GPUFIN" in config)
        target_bg = int(room["requested_case"].get("target_bg", 0))
        room["background_matches"] = not target_bg or room["backgrounds_observed"] == [target_bg]
        room["evidence_complete"] = bool(room["completed"] and room["background_matches"] and room["measured_frames"] >= 30
                                         and len([c for c in capture_files if c["valid"]]) >= 2 and room["draw_max"])
        results.append(room)
    covered = {(r["world"], r["scene"]) for r in results if r["evidence_complete"]}
    covered_cases = {(r["world"], r["scene"], int(r["requested_case"].get("visit", 0)),
                      int(r["requested_case"].get("target_bg", 0))) for r in results if r["evidence_complete"]}
    return dict(schema=1, rooms=results, alerts=alerts, eligible=sum(r["eligible"] for r in census["rooms"]),
                catalog_rooms=len(census["rooms"]),
                completed=len(covered), missing=[{k: r[k] for k in ("world", "scene", "asset")}
                                                for r in census["rooms"] if r["eligible"] and (r["world"], r["scene"]) not in covered],
                excluded=[{k: r[k] for k in ("world", "scene", "asset")} for r in census["rooms"] if not r["eligible"]],
                cases_completed=len(covered_cases), cases_total=len(census.get("cases", [])),
                missing_assets=census.get("missing_assets", []),
                extra_assets=census["extra_assets"], limits=census["limits"])


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    sub = ap.add_subparsers(dest="command", required=True)
    c = sub.add_parser("catalog")
    c.add_argument("--exe", type=Path, default=Path("game/sh4.exe"))
    c.add_argument("--data", type=Path, default=Path("D:/Games/Silent Hill 4/data"))
    c.add_argument("--out", type=Path, default=Path("build/room-audit"))
    c.add_argument("--world", type=int)
    c.add_argument("--first", type=int, default=1)
    c.add_argument("--last", type=int, default=255)
    c.add_argument("--cases", choices=("base", "all", "extra"), default="base")
    r = sub.add_parser("report")
    r.add_argument("catalog", type=Path)
    r.add_argument("logs", nargs="+", type=Path)
    r.add_argument("--out", type=Path, required=True)
    r.add_argument("--csv", type=Path, help="Optional one-row-per-attempt measurements, preserving duplicate cases/builds")
    args = ap.parse_args()
    if args.command == "catalog":
        result = catalog(args.exe, args.data)
        args.out.mkdir(parents=True, exist_ok=True)
        (args.out / "catalog.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
        selected = [r for r in result["cases"] if
                    (args.cases == "all" or (r["kind"] == "base") == (args.cases == "base")) and
                    (args.world is None or r["world"] == args.world) and args.first <= r["scene"] <= args.last]
        (args.out / "tour.txt").write_text("".join(f'{r["world"]} {r["scene"]} {r["entry"]} {r["visit"]} {r["bg"]}\n' for r in selected), encoding="ascii")
        print(f'{len(result["rooms"])} table rooms, {len(result["extra_assets"])} extra assets, {len(selected)} tour entries')
    else:
        result = report(json.loads(args.catalog.read_text(encoding="utf-8")), args.logs)
        args.out.write_text(json.dumps(result, indent=2), encoding="utf-8")
        if args.csv:
            fields = ["platform", "world", "scene", "visit", "target_bg", "evidence_complete", "visual_status",
                      "measured_frames", "mean_ms", "p95_ms", "max_ms", "all_phase_peak_ms", "loading_peak_ms", "shader_link_peak_ms", "heap_peak_kb", "draw_mean", "load_ms",
                      "validation_enabled", "movie_state_logged", "binary_sha256", "log"]
            with args.csv.open("w", newline="", encoding="utf-8-sig") as stream:
                writer = csv.DictWriter(stream, fieldnames=fields)
                writer.writeheader()
                for room in result["rooms"]:
                    row = {field: room.get(field) for field in fields}
                    row.update(visit=room["requested_case"].get("visit", 0), target_bg=room["requested_case"].get("target_bg", 0),
                               load_ms=next((event.get("load_ms") for event in room["events"] if event["event"] == "arrive"), None))
                    writer.writerow(row)
        print(f'{result["completed"]}/{result["eligible"]} rooms with completed measurements; {len(result["alerts"])} alert lines')


if __name__ == "__main__":
    main()
