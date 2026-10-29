"""Inventory the supported GOG's in-engine cinematic streams, without copying assets."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'recomp'))
from pe import PE

SUPPORTED='cf7081a6c78dbe51f6dc2e1c9bd978d8486f1d7b59d7a3b88e027c9b39f21dd0'


def catalog(exe,demo):
    pe=PE(exe)
    if hashlib.sha256(pe.data).hexdigest()!=SUPPORTED:
        raise ValueError('Only GOG v5 is supported')
    stages={i:pe.cstr(0x5bf860-4*i) for i in range(1,11)}
    drivers={}
    for address in range(0x5b7000,0x6a3000-44,4):
        try:words=struct.unpack('<11I',pe.read(address,44))
        except (struct.error,TypeError):continue
        if words[2:4]!=(0x4cd170,0x4cd1b0):continue
        files=[]
        if 0x5b7000<=words[6]<=0x6a3000-4:
            for n in range(64):
                word=struct.unpack('<I',pe.read(words[6]+n*4,4))[0]
                if word==0x0fffffff:break
                if word&0xf000f000!=0xf000f000:break
                files.append(hex(word))
            else:continue
            if word==0x0fffffff:
                drivers.setdefault(words[7],[]).append(dict(address=hex(address),files=files,
                    end_callback=hex(words[5]),narrative_setup='unverified'))
    rows=[]
    seen=set()
    # DemoGetFileName 0x509670 and DemoGetFileNumber 0x5097c0 use
    # {int16 id,int16 merge_index,uint8 stage,pad}, terminated by id==0.
    for i in range(512):
        b=pe.read(0x650640+i*6,6)
        if not b or len(b)!=6:raise ValueError('Truncated executable table')
        ident,index,stage,pad=struct.unpack('<hhBB',b)
        if ident==0:break
        if ident<0 or ident in seen or stage not in stages or index<0:
            raise ValueError(f'Unsupported cinematic descriptor at {i}')
        seen.add(ident)
        row=dict(id=ident,stage=stage,world=stage,prefix=stages[stage],merge_index=index,
                 table_address=hex(0x650640+i*6),eligible=False,common_drivers=drivers.get(ident,[]))
        path=demo/(stages[stage]+'.sh4')
        row['archive']=path.name
        try:
            size=path.stat().st_size
            with path.open('rb') as f:
                head=f.read(8)
                if len(head)!=8 or head[:4]!=b'SH4\0':raise ValueError('Bad SH4 container header')
                count=struct.unpack_from('<I',head,4)[0]
                if not 0<count<=4096 or index>=count:raise ValueError('Merge index outside container')
                f.seek(8+index*8);slot=f.read(8)
                if len(slot)!=8:raise ValueError('Truncated container offset')
                offset,length=struct.unpack('<II',slot)
                if length<40 or offset>size-length:raise ValueError('Cinematic entry outside archive')
                f.seek(offset);raw=f.read(40)
                magic,version,start,end,actors,characters,frame_bytes,frames,*translation=struct.unpack('<Hhhhiiii4f',raw)
                if magic!=0xde30:raise ValueError(f'Not a DE30 cinematic header: {magic:04x}')
                if version!=1 or not 0<=actors<=16 or end<start or characters<40 or frames<40 or frame_bytes<=0:
                    raise ValueError('Unsupported cinematic header fields')
                if characters+actors*8>length or frames+frame_bytes>length:raise ValueError('Cinematic payload outside entry')
                row.update(eligible=True,offset=offset,length=length,version=version,first_frame=start,last_frame=end,
                           nominal_frame_span=end-start,nominal_seconds=(end-start)/30,
                           actors=actors,character_offset=characters,frame_bytes=frame_bytes,
                           frame_offset=frames,translation=translation)
        except (OSError,ValueError,struct.error) as e:
            row['reason']=str(e)
        rows.append(row)
    else:raise ValueError('Unterminated cinematic table')
    return dict(exe_sha256=SUPPORTED,source='GOG v5 table and local SH4 stream headers',
                limits=['Stage identifies the archive/world, not a verified narrative room.',
                        'Nominal frame span excludes loading, preroll, skips and event callbacks.',
                        'Eligibility describes the file format; playback is unverified until captured.'],
                cinematics=rows)


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--exe',type=Path,default=ROOT/'game/sh4.exe')
    ap.add_argument('--demo',type=Path,default=ROOT/'build/pc/game/demo')
    ap.add_argument('--out',type=Path,required=True)
    args=ap.parse_args()
    result=catalog(args.exe,args.demo)
    args.out.parent.mkdir(parents=True,exist_ok=True)
    args.out.write_text(json.dumps(result,indent=2,allow_nan=False),encoding='utf-8')
    rows=result['cinematics'];valid=[r for r in rows if r['eligible']]
    common=sum(bool(r['common_drivers']) and r['eligible'] for r in rows)
    print(f'{len(rows)} cinematic IDs; {len(valid)} validated stream headers; {len(rows)-len(valid)} unsupported/missing; {common} common-event candidates')
    for r in rows:
        if not r['eligible']:print(r['id'],r['reason'])


if __name__=='__main__':main()
