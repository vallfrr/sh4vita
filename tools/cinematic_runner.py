"""Laboratory cinematic sequence in one explicitly chosen room (PC or Vita).

Narrative triggers and custom event drivers are not reproduced. All evidence
must be reviewed; completion is not a visual correctness verdict.
"""
import argparse
import hashlib
import io
import json
from pathlib import Path
import shutil
import time
from room_runner import ROOT, Vita, run_batch, save_vita, restore_vita
from cinematic_audit import catalog


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('platform',choices=['pc','vita'])
    ap.add_argument('--out',type=Path,required=True)
    ap.add_argument('--ids',required=True,help='Ordered comma-separated common-event demo IDs')
    ap.add_argument('--world',type=int,required=True)
    ap.add_argument('--scene',type=int,required=True)
    ap.add_argument('--entry',type=int,default=0)
    ap.add_argument('--visit',type=int,choices=[0,1],default=0)
    ap.add_argument('--ip',default='192.168.1.175')
    ap.add_argument('--eboot',type=Path)
    ap.add_argument('--pc-exe',type=Path)
    ap.add_argument('--symbols',type=Path,help='Symbols paired with the chosen Vita eboot')
    ap.add_argument('--env',action='append',default=[])
    ap.add_argument('--seconds',type=int,default=1800)
    ap.add_argument('--shot-every',type=int,default=120,help='Capture interval in logical frames; 0 for performance-only playback')
    args=ap.parse_args()
    args.out=args.out.resolve()
    if not args.out.is_relative_to(ROOT/'build'):
        ap.error('Evidence directory must be inside ignored build/')
    if args.out.exists():ap.error('Output directory already exists')
    try:ids=[int(s) for s in args.ids.split(',')]
    except ValueError:ap.error('Invalid ID list')
    if not ids or len(ids)>32 or any(not 0<i<32768 for i in ids):ap.error('Invalid ID list')
    if not 1<=args.world<=10 or not 1<=args.scene<=255 or not 0<=args.entry<=255:ap.error('Invalid room')
    if args.shot_every not in (0,) and not 30<=args.shot_every<=3000:ap.error('Invalid shot interval')
    if any(s.startswith(('SH4_AUDIT_TOUR=','SH4_DEMO=','SH4_DEMO_SHOT_EVERY=')) for s in args.env):ap.error('Tour/demo schedule is owned by this runner')
    if (ROOT/'build/room-audit/STOP_ALL').exists():raise SystemExit('STOP_ALL: no test started')
    if args.platform=='vita' and not args.eboot:ap.error('--eboot is required for an explicitly identified test build')
    inventory=catalog(ROOT/'game/sh4.exe',ROOT/'build/pc/game/demo')
    entries={r['id']:r for r in inventory['cinematics']}
    for ident in ids:
        r=entries.get(ident)
        if not r or not r['eligible'] or not r['common_drivers'] or r['world']!=args.world:
            ap.error(f'ID {ident}: absent/unsupported/custom driver or wrong archive world; no launch performed')
    args.out.mkdir(parents=True)
    (args.out/'selected-catalog.json').write_text(json.dumps([entries[i] for i in ids],indent=2),encoding='utf-8')
    binary=args.eboot if args.platform=='vita' else args.pc_exe or ROOT/'build/pc/sh4.exe'
    (args.out/'binary.sha256').write_text(hashlib.sha256(binary.read_bytes()).hexdigest(),encoding='ascii')
    if args.symbols:
        shutil.copyfile(args.symbols,args.out/('sh4'+args.symbols.suffix))
        (args.out/'symbols.sha256').write_text(hashlib.sha256(args.symbols.read_bytes()).hexdigest(),encoding='ascii')
    args.cinematic_ids=ids
    args.env+=['SH4_DEMO='+','.join(f'4000:{i}' for i in ids)]
    args.env+=['SH4_DEMO_SHOT_EVERY='+str(args.shot_every)]
    args.settle=180;args.dwell=450;args.no_live=True;args.move=False;args.sweep=False
    args.batch_seconds=args.seconds;args.startup_seconds=120;args.stall_seconds=120;args.hold_seconds=0
    vita=Vita(args.ip) if args.platform=='vita' else None
    backup=None;original=None
    try:
        if vita:
            backup=save_vita(vita,args.out/'original')
            with vita.connect() as ftp:
                stream=io.BytesIO();ftp.retrbinary('RETR ux0:/app/SILH00004/eboot.bin',stream.write)
            original=stream.getvalue()
            (args.out/'original/eboot.bin').write_bytes(original)
            vita.command('quit all')
            vita.write('ux0:/app/SILH00004/eboot.bin',args.eboot.read_bytes(),absolute=True)
        result=run_batch(args,[f'{args.world} {args.scene} {args.entry} {args.visit} 0\n'],args.out/'sequence',vita)
        print(json.dumps(result,indent=2),flush=True)
        if result['outcome']!='finished':raise RuntimeError('Cinematic sequence did not finish; inspect evidence')
    finally:
        if vita and backup:
            restore_vita(vita,backup)
            if original is not None:
                vita.write('ux0:/app/SILH00004/eboot.bin',original,absolute=True)
                with vita.connect() as ftp:
                    stream=io.BytesIO();ftp.retrbinary('RETR ux0:/app/SILH00004/eboot.bin',stream.write)
                if stream.getvalue()!=original:raise RuntimeError('Original eboot restore verification failed')
            vita.command('launch SILH00004')
            time.sleep(4)
            (args.out/'normal-log.txt').write_bytes(vita.read('sh4log.txt'))
            (args.out/'restored.json').write_text(json.dumps(dict(
                save_sha256=hashlib.sha256(vita.read('save/sh4.sav')).hexdigest(),
                expected_save_sha256=backup['save/sh4.sav']['sha256'],
                eboot_sha256=hashlib.sha256(original).hexdigest() if original is not None else None,
                normal_launch_sent=True),indent=2),encoding='utf-8')
            print('Original save, eboot and configuration restored; normal launch sent.',flush=True)


if __name__=='__main__':main()
