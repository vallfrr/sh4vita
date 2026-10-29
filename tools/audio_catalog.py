"""Inventory local audio assets; never copy audio or treat a header as playback proof."""
import argparse
import collections
import json
from pathlib import Path
import struct


def wave_header(stream, offset, length):
    stream.seek(offset); header = stream.read(min(length, 12))
    if len(header) < 12 or header[:4] != b'RIFF' or header[8:12] != b'WAVE':
        return None
    result = dict(kind='riff-wave', declared_riff_bytes=struct.unpack_from('<I',header,4)[0]+8,
                  playback_verified=False, chunks=[])
    cursor = 12
    while cursor + 8 <= length:
        stream.seek(offset+cursor); chunk = stream.read(8)
        if len(chunk) != 8: break
        tag, size = chunk[:4], struct.unpack_from('<I',chunk,4)[0]
        if size > length-cursor-8:
            result['chunk_exceeds_entry'] = True; break
        if tag == b'fmt ' and size >= 16:
            fmt = stream.read(16)
            kind, channels, rate, byte_rate, align, bits = struct.unpack('<HHIIHH',fmt)
            result.update(format=kind,channels=channels,rate=rate,byte_rate=byte_rate,alignment=align,bits=bits)
        if tag == b'data':
            result['data_bytes'] = size
            if result.get('byte_rate'): result['nominal_seconds'] = size/result['byte_rate']
        result['chunks'].append(dict(tag=tag.decode('ascii',errors='replace'),bytes=size))
        cursor += 8+size+(size&1)
    result['riff_size_matches_entry'] = result['declared_riff_bytes'] == length
    return result


def inventory(game):
    rows=[];containers=[]
    files=[game/'sound/sound.pac',*sorted((game/'demo').glob('*.sh4'))]
    for path in files:
        size=path.stat().st_size
        with path.open('rb') as stream:
            header=stream.read(8)
            if header[:4] not in (b'SDPA',b'SH4\0'):
                containers.append(dict(path=str(path.relative_to(game)),status='unrecognized'));continue
            count=struct.unpack_from('<I',header,4)[0];stride=12 if header[:4]==b'SDPA' else 8
            if not 0<count<=4096 or 8+count*stride>size:raise ValueError('Invalid archive directory: '+str(path))
            directory=stream.read(count*stride)
            containers.append(dict(path=str(path.relative_to(game)),entries=count,bytes=size,status='directory-bounds-checked'))
            for ident in range(count):
                offset,length=struct.unpack_from('<II',directory,ident*stride)
                if offset>size or length>size-offset:raise ValueError('Entry outside archive: '+str(path))
                row=dict(archive=str(path.relative_to(game)),entry=ident,offset=offset,bytes=length,playback_verified=False)
                if not length: row['kind']='empty';rows.append(row);continue
                sound=wave_header(stream,offset,length)
                if sound:row.update(sound)
                else:
                    stream.seek(offset);raw=stream.read(min(length,12))
                    if len(raw)>=12 and raw[0] in (1,2) and 8000<=struct.unpack_from('<H',raw,1)[0]<=48000 and struct.unpack_from('<I',raw,3)[0]==length-12:
                        row.update(kind='game-audio-header-candidate',channels=raw[0],rate=struct.unpack_from('<H',raw,1)[0],payload_bytes=length-12,codec_mode=raw[7],codec_verified=False)
                    elif raw[:2]==b'\x30\xde':row['kind']='cinematic-stream'
                    else:row['kind']='other-or-unclassified'
                rows.append(row)
    return dict(schema=1,source='local original archives; offsets and headers only',containers=containers,entries=rows,
                counts=dict(collections.Counter(r['kind'] for r in rows)),
                limits=['Header/directory inspection is not decode or playback validation.',
                        'Mixed archives include cinematic, empty and unclassified resources.',
                        'Sound effects, music, dialogue and movie audio require separate runtime coverage.',
                        'RIFF length differences are reported, not silently repaired.'])


if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--game',type=Path,required=True)
    ap.add_argument('--out',type=Path,required=True)
    args=ap.parse_args();root=Path(__file__).resolve().parents[1]
    if not args.out.resolve().is_relative_to(root/'build'):ap.error('Game-derived catalog must remain in ignored build/')
    result=inventory(args.game);args.out.parent.mkdir(parents=True,exist_ok=True)
    args.out.write_text(json.dumps(result,indent=2),encoding='utf-8')
    print(json.dumps(dict(containers=len(result['containers']),counts=result['counts']),indent=2))
