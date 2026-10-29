"""Run isolated room-audit batches. Results and game-derived manifests stay under build/."""
import argparse
import ftplib
import gzip
import hashlib
import io
import json
import os
from pathlib import Path
import re
import shutil
import socket
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
ROUTE = "800:01:3,1300:1c:3,1500:1c:3,1700:1c:3,3200:1c:3,3400:1c:3,1000000:ff:1"


class Vita:
    def __init__(self, ip):
        self.ip = ip

    def connect(self):
        ftp = ftplib.FTP()
        ftp.connect(self.ip, 1337, timeout=15)
        ftp.login()
        return ftp

    def command(self, command):
        with socket.create_connection((self.ip, 1338), timeout=10) as sock:
            sock.sendall((command + "\n").encode("ascii"))
        time.sleep(2)

    def read(self, name, offset=0):
        out = io.BytesIO()
        with self.connect() as ftp:
            ftp.retrbinary("RETR ux0:/data/sh4/" + name, out.write, rest=offset or None)
        return out.getvalue()

    def write(self, name, data, absolute=False):
        with self.connect() as ftp:
            ftp.storbinary("STOR " + (name if absolute else "ux0:/data/sh4/" + name), io.BytesIO(data))

    def delete(self, name):
        with self.connect() as ftp:
            try:
                ftp.delete("ux0:/data/sh4/" + name)
            except ftplib.error_perm as e:
                if not str(e).startswith("550"):
                    raise


def save_vita(vita, out):
    out.mkdir(parents=True, exist_ok=True)
    files = {}
    for name in ("env.txt", "save/sh4.sav", "save/sh4.sav.bak", "save/sh4.sav.new", "save/sh4.sav.prev"):
        try:
            data = vita.read(name)
        except ftplib.error_perm as e:
            if str(e).startswith("550"):
                continue
            raise
        dest = out / name.replace("/", "_")
        dest.write_bytes(data)
        # Read twice before any launch or save removal; abort on an unstable transfer.
        if vita.read(name) != data:
            raise RuntimeError(f"Backup verification failed: {name}")
        files[name] = dict(path=str(dest), sha256=hashlib.sha256(data).hexdigest(), size=len(data))
    if "save/sh4.sav" not in files or files["save/sh4.sav"]["size"] != 694288:
        raise RuntimeError("Expected original sh4.sav is absent or has an unexpected size; no launch performed")
    (out / "backup.json").write_text(json.dumps(files, indent=2), encoding="utf-8")
    return files


def restore_vita(vita, files):
    vita.command("quit all")
    for suffix in (".bak", ".new", ".prev"):
        vita.delete("save/sh4.sav" + suffix)
    original = Path(files["save/sh4.sav"]["path"]).read_bytes()
    vita.write("save/sh4.sav", original)
    if hashlib.sha256(vita.read("save/sh4.sav")).hexdigest() != files["save/sh4.sav"]["sha256"]:
        raise RuntimeError("RESTORE HASH MISMATCH")
    if "env.txt" in files:
        vita.write("env.txt", Path(files["env.txt"]["path"]).read_bytes())
    else:
        vita.delete("env.txt")
    for name in ("ctl.txt", "key.txt", "shotnow", "audit-tour.txt"):
        vita.delete(name)


def run_batch(args, lines, folder, vita):
    folder.mkdir(parents=True)
    manifest = folder / "tour.txt"
    manifest.write_text("".join(lines), encoding="ascii")
    env = dict(SH4_AUDIT="1", SH4_AUDIT_START="3700", SH4_AUDIT_SETTLE=str(args.settle),
               SH4_AUDIT_DWELL=str(args.dwell), SH4_LIVE="1", SH4_STATS="1", SH4_INPUT=ROUTE)
    if args.move:
        env["SH4_AUDIT_MOVE"] = "1"
    if args.sweep:
        env["SH4_AUDIT_SWEEP"] = "1"
    for assignment in args.env:
        key, sep, value = assignment.partition("=")
        if not sep or not re.fullmatch(r"SH4_[A-Z0-9_]+", key):
            raise ValueError(f"Invalid --env {assignment}")
        env[key] = value
    if args.no_live:
        env.pop("SH4_LIVE", None)
    logpath = folder / "sh4log.txt"
    proc = None
    stream = None
    if vita:
        env["SH4_AUDIT_GZIP"] = "1"
        vita.command("quit all")
        for suffix in ("", ".new", ".bak", ".prev"):
            vita.delete("save/sh4.sav" + suffix)
        for name in ("ctl.txt", "key.txt", "shotnow", "sh4log.txt"):
            vita.delete(name)
        vita.write("audit-tour.txt", manifest.read_bytes())
        env["SH4_AUDIT_TOUR"] = "ux0:data/sh4/audit-tour.txt"
        vita.write("env.txt", "".join(f"{k}={v}\n" for k, v in env.items()).encode("ascii"))
        vita.command("launch SILH00004")
    else:
        game = ROOT / "build/pc/game"
        for p in (game / "save").glob("sh4.sav*"):
            p.unlink()
        for p in game.glob("audit_*.bmp"):
            p.unlink()
        env.update(SH4_AUDIT_TOUR=str(manifest), SH4_VTIME="1", SH4_HEADLESS="1", SH4_RES="640x362", SH4_VBO="1")
        runtime_env = os.environ.copy()
        for key in list(runtime_env):
            if key.startswith("SH4_"):
                del runtime_env[key]
        runtime_env.update(env)
        # Keep 32-bit DLLs from unrelated Windows tools out of this 64-bit MinGW process.
        runtime_env["PATH"] = str(ROOT.parent / "tools/mingw64/bin") + os.pathsep + runtime_env.get("PATH", "")
        stream = (folder / "console.txt").open("wb")
        pc_log = ROOT / "build/pc/sh4log.txt"
        if pc_log.exists():
            pc_log.unlink()
        proc = subprocess.Popen([str(args.pc_exe or ROOT / "build/pc/sh4.exe"), str(game)], cwd=ROOT / "build/pc",
                                env=runtime_env, stdout=stream, stderr=subprocess.STDOUT)
    (folder / "config.json").write_text(json.dumps(env, indent=2), encoding="utf-8")
    began = last_progress = time.monotonic()
    last_frame, last_index, offset, announced = 0, 0, 0, 0
    outcome = "unknown"
    text = ""
    try:
        while time.monotonic() - began < args.batch_seconds:
            time.sleep(5 if vita else 1)
            try:
                if vita:
                    chunk = vita.read("sh4log.txt", offset)
                    if chunk:
                        with logpath.open("ab") as f:
                            f.write(chunk)
                        offset += len(chunk)
                    text = logpath.read_text(encoding="utf-8", errors="replace") if logpath.exists() else ""
                else:
                    text = pc_log.read_text(encoding="utf-8", errors="replace") if pc_log.exists() else ""
            except (OSError, EOFError, ftplib.Error) as e:
                print(f"{folder.name}: log transfer retry: {e}", flush=True)
                continue
            frames = re.findall(r"\[(?:audit-render|audit-heartbeat)\] frame=(\d+)|\[audit\] event=frame .*?frame=(\d+)|\[stats\] presents=(\d+)", text)
            frame = int(next(x for x in frames[-1] if x)) if frames else 0
            if frame != last_frame:
                last_frame, last_progress = frame, time.monotonic()
            indexes = re.findall(r"\[audit\] event=request index=(\d+)", text)
            last_index = int(indexes[-1]) if indexes else 0
            if last_index != announced:
                print(f"{folder.name}: room {last_index}/{len(lines)}, frame {frame}", flush=True)
                announced = last_index
            cinematic_ids = getattr(args, "cinematic_ids", None)
            if cinematic_ids and "[cinematic] event=refused " in text:
                outcome = "cinematic-refused"
                break
            completed_clips = [int(x) for x in re.findall(r"\[cinematic\] event=complete index=\d+ id=(\d+)", text)] if cinematic_ids else []
            finished = completed_clips == cinematic_ids if cinematic_ids else "event=finished " in text
            if finished:
                outcome = "finished"
                if args.hold_seconds:
                    print(f"{folder.name}: holding {args.hold_seconds}s for live inspection", flush=True)
                    until = time.monotonic() + args.hold_seconds
                    while time.monotonic() < until and not (args.out / "STOP").exists():
                        time.sleep(1)
                break
            if (args.out / "STOP").exists() or (ROOT / "build/room-audit/STOP_ALL").exists():
                outcome = "operator-stop"
                break
            if "FATAL:" in text:
                outcome = "fatal"
                break
            if proc and proc.poll() is not None:
                outcome = f"exit-{proc.returncode}"
                break
            if time.monotonic() - last_progress > (args.stall_seconds if last_frame else args.startup_seconds):
                outcome = "no-frame-progress"
                break
        else:
            outcome = "batch-wall-timeout"
    finally:
        if proc:
            if proc.poll() is None:
                proc.terminate()
                proc.wait(timeout=10)
            stream.close()
            if pc_log.exists():
                shutil.copy2(pc_log, logpath)
            for p in game.glob("audit_*.bmp"):
                shutil.copy2(p, folder / p.name)
        if vita:
            vita.command("quit all")
            final_log = vita.read("sh4log.txt")
            logpath.write_bytes(final_log)
            text = final_log.decode("utf-8", errors="replace")
            # A stopped process has flushed its last capture; collect only this batch's requests.
            captures = set(re.findall(r"path=ux0:data/sh4/(audit_[\w.]+)", text))
            for name in sorted(captures):
                try:
                    data = vita.read(name)
                    (folder / name).write_bytes(data)
                    raw = gzip.decompress(data) if name.endswith(".gz") else data
                    if len(raw) != 18 + 960 * 544 * 4:
                        print(f"INCOMPLETE CAPTURE {name}: {len(raw)} decoded bytes", flush=True)
                    vita.delete(name)
                except (OSError, EOFError, ftplib.Error) as e:
                    print(f"MISSING CAPTURE {name}: {e}", flush=True)
    summary = dict(outcome=outcome, requested=last_index, entries=len(lines), last_frame=last_frame,
                   elapsed_seconds=round(time.monotonic() - began, 2))
    (folder / "result.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
    print(f"{folder.name}: {summary}", flush=True)
    return summary


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("platform", choices=("pc", "vita"))
    ap.add_argument("--tour", required=True, type=Path)
    ap.add_argument("--out", required=True, type=Path)
    ap.add_argument("--eboot", type=Path)
    ap.add_argument("--pc-exe", type=Path, help="Optional PC diagnostic executable; same isolated mirror and runtime DLL path")
    ap.add_argument("--ip", default="192.168.1.175")
    ap.add_argument("--chunk", type=int, default=20)
    ap.add_argument("--group-world", action="store_true")
    ap.add_argument("--no-restart", action="store_true", help="Keep a single session: stop at the first incomplete batch")
    ap.add_argument("--settle", type=int, default=120)
    ap.add_argument("--dwell", type=int, default=240)
    ap.add_argument("--move", action="store_true")
    ap.add_argument("--sweep", action="store_true")
    ap.add_argument("--env", action="append", default=[])
    ap.add_argument("--hold-seconds", type=int, default=0)
    ap.add_argument("--no-live", action="store_true")
    ap.add_argument("--batch-seconds", type=int, default=1200)
    ap.add_argument("--stall-seconds", type=int, default=45)
    ap.add_argument("--startup-seconds", type=int, default=100,
                    help="maximum time before the first game frame (increase for deliberate shader preparation)")
    args = ap.parse_args()
    if args.pc_exe:
        args.pc_exe = args.pc_exe.resolve()
    args.out = args.out.resolve()
    args.out.mkdir(parents=True, exist_ok=False)
    lines = [line for line in args.tour.read_text(encoding="ascii").splitlines(keepends=True)
             if line.strip() and not line.startswith("#")]
    vita = Vita(args.ip) if args.platform == "vita" else None
    backup = save_vita(vita, args.out / "original") if vita else None
    binary = args.eboot if vita else args.pc_exe or ROOT / "build/pc/sh4.exe"
    if not binary or not binary.is_file():
        raise RuntimeError("Compiled binary required")
    (args.out / "binary.sha256").write_text(hashlib.sha256(binary.read_bytes()).hexdigest(), encoding="ascii")
    if vita:
        symbols = {}
        for suffix in (".velf", ".elf"):
            source = binary.with_suffix(suffix)
            if source.is_file():
                target = args.out / ("sh4" + suffix)
                shutil.copy2(source, target)
                symbols[target.name] = dict(source=str(source.resolve()), sha256=hashlib.sha256(target.read_bytes()).hexdigest())
        (args.out / "symbols.json").write_text(json.dumps(symbols, indent=2), encoding="utf-8")
    try:
        if vita:
            vita.command("quit all")
            vita.write("ux0:/app/SILH00004/eboot.bin", binary.read_bytes(), absolute=True)
        cursor = 0
        batch = 0
        failures = 0
        while cursor < len(lines):
            batch += 1
            selected = lines[cursor:cursor + args.chunk]
            if args.group_world:
                w = selected[0].split()[0]
                selected = selected[:next((i for i, line in enumerate(selected) if line.split()[0] != w), len(selected))]
            result = run_batch(args, selected, args.out / f"batch_{batch:03}", vita)
            if args.no_restart and result["outcome"] != "finished":
                (args.out / "progress.json").write_text(json.dumps(dict(
                    consumed=cursor + result["requested"], total=len(lines), batch=batch,
                    last_result=result), indent=2), encoding="utf-8")
                break
            if result["outcome"] == "operator-stop" or (args.out / "STOP").exists():
                break
            consumed = len(selected) if result["outcome"] == "finished" else result["requested"]
            if consumed == 0:
                failures += 1
                if failures >= 3:
                    raise RuntimeError("Three launches without any room request; stopping and restoring")
            else:
                failures = 0
                cursor += consumed
            (args.out / "progress.json").write_text(json.dumps(dict(consumed=cursor, total=len(lines),
                                                                   batch=batch, last_result=result), indent=2), encoding="utf-8")
    finally:
        if vita and backup:
            restore_vita(vita, backup)
            print("Original save restored and SHA256 verified; normal env restored; application closed.", flush=True)


if __name__ == "__main__":
    main()
