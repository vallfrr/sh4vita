"""Locate cinematic event placements in the original GOG scene command streams.

0x52c4c0 indexes commands of kind0 by scene. 0x4f0af1 passes the current
scene to 0x52ca00, which consumes that index. Property24 binds an event driver.
This is placement evidence, not proof of narrative preconditions or playback.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'recomp'))
from pe import PE
from cinematic_audit import SUPPORTED


def placements(pe, root):
    if not 0x5b7000 <= root < 0x6a3000:
        raise ValueError('Scene stream pointer outside original static data')
    cursor, scene = root, None
    for _ in range(50000):
        token = pe.u32(cursor)
        if token == 0xf0f0f0f0:
            return
        kind, count = token & 65535, token >> 16
        if not 1 <= count <= 64 or cursor+4+4*count > 0x6a3000:
            raise ValueError(f'Unsupported scene command {token:08x} at {cursor:08x}')
        if kind == 0:
            scene = pe.u32(cursor+4)
            if count != 1 or scene > 255:
                raise ValueError('Unsupported scene marker')
        if kind == 24:
            if count != 1 or scene is None:
                raise ValueError('Driver without scene')
            yield dict(scene=scene, command_address=hex(cursor), driver=pe.u32(cursor+4))
        cursor += 4+4*count
    raise ValueError('Unterminated scene command stream')


def catalog(exe, cinematics):
    pe = PE(exe)
    if hashlib.sha256(pe.data).hexdigest() != SUPPORTED:
        raise ValueError('Only supported GOG v5 executable is accepted')
    common = {int(d['address'], 16): r['id']
              for r in cinematics['cinematics'] for d in r['common_drivers']}
    found = []
    for visit, table in ((0, 0x6553a4), (1, 0x6553d8)):
        for world in range(1, 11):
            root = pe.u32(table+world*4)
            for item in placements(pe, root):
                if item['driver'] in common:
                    found.append(dict(world=world, visit=visit, id=common[item['driver']],
                        scene=item['scene'], driver=hex(item['driver']),
                        command_address=item['command_address'], stream_root=hex(root)))
    return dict(exe_sha256=SUPPORTED, placements=found,
        limits=['Scene placement does not establish narrative flags or prerequisites.',
                'The two tables are selected through original 0x4fc830; visit meaning must match the room runner.',
                'Common drivers only; custom cinematic event setup remains separate.'])


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--exe', type=Path, required=True)
    ap.add_argument('--cinematics', type=Path, required=True)
    ap.add_argument('--out', type=Path, required=True)
    args = ap.parse_args()
    if not args.out.resolve().is_relative_to(ROOT/'build'):
        ap.error('Derived addresses and placements must remain under ignored build/')
    result = catalog(args.exe, json.loads(args.cinematics.read_text()))
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2))
    print(f'{len(result["placements"])} placements, '
          f'{len({r["id"] for r in result["placements"]})} common cinematic IDs')
