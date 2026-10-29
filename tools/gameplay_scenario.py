"""Bounded gameplay-event replay, distinct from a resource-only room tour.

PC only: does not claim ownership of the Vita or alter its running candidate.
Runtime game data and evidence must remain under ignored build/.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]


def replay(config_path, output, target_frame, seconds, tunnel=None):
    output = output.resolve()
    if not output.is_relative_to(ROOT / 'build'):
        raise ValueError('Evidence must stay under build/')
    if (ROOT / 'build/room-audit/STOP_ALL').exists():
        raise RuntimeError('STOP_ALL')
    # Refuse another running reference game, including a reused PID.
    query = subprocess.run(['powershell', '-NoProfile', '-Command',
        "Get-CimInstance Win32_Process | Where-Object { $_.Name -like 'sh4*.exe' } | Select-Object ProcessId,CommandLine | ConvertTo-Json -Compress"],
        capture_output=True, text=True, check=True)
    if query.stdout.strip():
        raise RuntimeError('Another PC game is active: ' + query.stdout.strip())
    output.mkdir(parents=True, exist_ok=False)
    game = ROOT / 'build/pc/game'
    save = game / 'save'
    original = {p.name: p.read_bytes() for p in save.glob('sh4.sav*')}
    backup = output / 'original'; backup.mkdir()
    for name, data in original.items():
        (backup / name).write_bytes(data)
    shots_before = list(game.glob('audit_*.bmp'))
    for p in shots_before:
        shutil.move(str(p), str(backup / p.name))
    binary = ROOT / 'build/pc/sh4.exe'
    env = {k: v for k, v in os.environ.items() if not k.startswith('SH4_')}
    env.update(SH4_STATS='1', SH4_AUDIT='1', SH4_AUDIT_PLAY='1', SH4_HEADLESS='1',
               SH4_VTIME='1', SH4_RES='640x362', SH4_VBO='1', SH4_PC_GUEST_MB='166',
               SH4_TITLE_SELECT='1200:0')
    config = json.loads(config_path.read_text(encoding='utf-8'))
    if 'SH4_AUDIT_TOUR' in config:
        raise ValueError('This runner requires actual interaction input, not a room tour')
    env.update(config)
    env['PATH'] = str(ROOT.parent / 'tools/mingw64/bin') + os.pathsep + env.get('PATH', '')
    (output / 'config.json').write_text(json.dumps({k: v for k, v in env.items() if k.startswith('SH4_')}, indent=2))
    (output / 'binary.sha256').write_text(hashlib.sha256(binary.read_bytes()).hexdigest())
    proc = None; outcome = 'not-started'; frame = 0; text = ''; started = time.monotonic()
    try:
        # Fresh disposable PC state; no original save is used or modified in place.
        for p in save.glob('sh4.sav*'): p.unlink()
        with (output / 'console.txt').open('wb') as stream:
            proc = subprocess.Popen([str(binary), str(game)], cwd=output, env=env,
                                    stdout=stream, stderr=subprocess.STDOUT)
            last_progress = started
            while time.monotonic() - started < seconds:
                time.sleep(1)
                log = output / 'sh4log.txt'
                if log.exists(): text = log.read_text(errors='replace')
                frames = re.findall(r'\[audit-render\] frame=(\d+)', text)
                fresh = int(frames[-1]) if frames else 0
                if fresh != frame: frame = fresh; last_progress = time.monotonic()
                if 'FATAL:' in text or '[audit-error]' in text:
                    outcome = 'diagnostic-failure'; break
                if (output / 'STOP').exists() or (ROOT / 'build/room-audit/STOP_ALL').exists():
                    outcome = 'operator-stop'; break
                if frame >= target_frame: outcome = 'target-frame'; break
                if proc.poll() is not None: outcome = 'process-exit-' + str(proc.returncode); break
                if time.monotonic() - last_progress > 90: outcome = 'no-progress'; break
            else: outcome = 'wall-timeout'
    finally:
        if proc is not None and proc.poll() is None:
            proc.terminate(); proc.wait(timeout=15)
        log = output / 'sh4log.txt'
        if log.exists(): text = log.read_text(errors='replace')
        for p in game.glob('audit_*.bmp'):
            shutil.move(str(p), str(output / p.name))
        for p in save.glob('sh4.sav*'): p.unlink()
        for name, data in original.items(): (save / name).write_bytes(data)
        for p in shots_before: shutil.move(str(backup / p.name), str(p))
        assert {p.name: p.read_bytes() for p in save.glob('sh4.sav*')} == original
        (output / 'restored.json').write_text(json.dumps({n: hashlib.sha256(d).hexdigest() for n, d in original.items()}, indent=2))
    samples = [tuple(map(int, m.groups())) for m in re.finditer(
        r'\[audit-play\] frame=(\d+) world=(\d+) scene=(\d+) bg=(\d+)', text)]
    entered = [s for s in samples if tunnel is not None and s[1:3] == (10, tunnel)]
    exited = [s for s in samples if entered and s[0] > entered[-1][0] and s[1] != 10]
    actions = sorted(set(re.findall(r'\[audit-action\].*?descriptor=([0-9a-f]+).*?kind=(\d+)', text)))
    errors = re.findall(r'^.*(?:FATAL:|\[audit-error\]|WARN: malloc|allocation failed).*$', text, re.M)
    result = dict(outcome=outcome, frame=frame, elapsed_seconds=time.monotonic()-started,
                  entered_expected_tunnel=bool(entered), returned_from_tunnel=bool(exited),
                  action_candidates=[dict(descriptor=d,kind=int(k)) for d,k in actions],
                  action_discovery_is_not_execution=True, errors=errors,
                  normal_interaction_proven=bool(tunnel and entered and exited and not errors and outcome=='target-frame'),
                  full_story_clearance=False, visual_review_pending=True)
    (output / 'result.json').write_text(json.dumps(result, indent=2))
    print(json.dumps(result, indent=2), flush=True)
    return result


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--config', type=Path, required=True)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--frames', type=int, default=7100)
    ap.add_argument('--seconds', type=int, default=300)
    ap.add_argument('--tunnel', type=int, choices=range(1,12))
    args = ap.parse_args()
    r = replay(args.config, args.out, args.frames, args.seconds, args.tunnel)
    if r['outcome'] != 'target-frame' or (args.tunnel and not r['normal_interaction_proven']):
        raise SystemExit(1)
