"""Decode the local archive payloads against the preserved GOG block decoder.

Host regression only: this does not play sounds on Vita or validate scheduling.
No game data or decoded PCM is written. Generated reference remains local.
"""
import argparse
import ctypes as C
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time

from adpcm_test import ROOT, reference


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--cc', default='gcc')
    ap.add_argument('--game', type=Path, default=ROOT/'build/pc/game')
    ap.add_argument('--catalog', type=Path, required=True)
    ap.add_argument('--out', type=Path, required=True)
    args = ap.parse_args()
    if not args.out.resolve().is_relative_to(ROOT / 'build'):
        ap.error('Reports derived from game data must remain under build/')
    assert hashlib.sha256((ROOT/'game/sh4.exe').read_bytes()).hexdigest() == \
        'cf7081a6c78dbe51f6dc2e1c9bd978d8486f1d7b59d7a3b88e027c9b39f21dd0'
    rows = [r for r in json.loads(args.catalog.read_text())['entries']
            if r['kind'] == 'game-audio-header-candidate']
    head = r'''
#include "rt.h"
#include "audio_adpcm.h"
#include <stdlib.h>
static uint8_t memory[32768];
uint8_t *g_mem=memory; uint32_t g_mem_size=sizeof memory;
'''
    tail = r'''
/* The individual block algorithm is identical for left/right/mono. Compare
 * every complete 13-byte block, not an invented stream/container decoder. */
int compare_blocks(const uint8_t *data,unsigned count) {
    uint8_t *const mb=g_mem;
    for(unsigned i=0;i<count;i++) {
        const uint32_t src=0x100,dst=0x180,sp=0x4000;
        uint8_t actual[48]; uint32_t predictor;
        memset(memory+src-16,0xa5,13+32);
        memcpy(memory+src,data+13*i,13);
        memset(memory+dst-16,0xa5,64);
        memset(actual,0xa5,sizeof actual);
        sh4_adpcm_block(actual+8,memory+src,&predictor);
        CPU cpu={0};cpu.esp=sp;cpu.fcw=0x23f;
        W32(sp,0x40aada);W32(sp+4,dst);W32(sp+8,src);
        F_0040a880(&cpu);
        if(memcmp(actual+8,memory+dst,32)) return 1;
        if(cpu.esp!=sp+12) return 2;
        if(memcmp(memory+src,data+13*i,13)) return 3;
        for(unsigned j=0;j<8;j++) if(actual[j]!=0xa5 || actual[40+j]!=0xa5) return 4;
        for(unsigned j=0;j<16;j++)
            if(memory[src-16+j]!=0xa5 || memory[src+13+j]!=0xa5 ||
               memory[dst-16+j]!=0xa5 || memory[dst+32+j]!=0xa5) return 5;
    }
    return 0;
}
'''
    started = time.monotonic()
    result = dict(scope='host native versus preserved generated GOG block decoder',
                  vita_playback_verified=False, entries=[], total_blocks=0,
                  decoded_pcm_bytes_compared=0, all_payloads_compared=False)
    with tempfile.TemporaryDirectory(prefix='sh4-real-streams-', ignore_cleanup_errors=True) as tmp:
        src = Path(tmp)/'reference.c'
        src.write_text(head+reference()+tail)
        dll = Path(tmp)/('test.dll' if os.name=='nt' else 'test.so')
        subprocess.run([args.cc, '-O2', '-shared', '-fPIC',
                        '-I'+str(ROOT/'port/src/rt'), str(src),
                        str(ROOT/'port/src/rt/audio_adpcm.c'), '-o', str(dll), '-lm'], check=True)
        lib = C.CDLL(str(dll))
        lib.compare_blocks.argtypes = [C.c_void_p, C.c_uint]
        lib.compare_blocks.restype = C.c_int
        for n, row in enumerate(rows, 1):
            path = args.game/row['archive']
            digest = hashlib.sha256()
            blocks = 0
            with path.open('rb') as stream:
                stream.seek(row['offset'])
                header = stream.read(12)
                assert header[0] == row['channels']
                remaining = row['bytes']-12
                assert remaining % 13 == 0, (path, row['entry'], 'partial block')
                while remaining:
                    data = stream.read(min(13*4096, remaining))
                    assert len(data) and len(data)%13 == 0
                    digest.update(data)
                    status = lib.compare_blocks(data, len(data)//13)
                    assert status == 0, (path, row['entry'], blocks, status)
                    blocks += len(data)//13
                    remaining -= len(data)
            result['entries'].append(dict(archive=row['archive'], entry=row['entry'],
                blocks=blocks, payload_sha256=digest.hexdigest(), complete=True))
            result['total_blocks'] += blocks
            result['decoded_pcm_bytes_compared'] += blocks*32
            if n%50 == 0 or n == len(rows):
                print(f'{n}/{len(rows)} streams; {result["total_blocks"]} blocks match', flush=True)
        if os.name=='nt':
            import _ctypes
            _ctypes.FreeLibrary(lib._handle)
    result.update(all_payloads_compared=True, elapsed_seconds=time.monotonic()-started,
        limits=['Every complete 13-byte block compared, without resampling or mixing.',
                'The independent x86 oracle belongs to adpcm_test.py.',
                'Container dispatch, stereo order, seek/loop and Vita playback remain separate tests.',
                'This test makes no assertion about the meaning of header byte 7.'])
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2))
    print(json.dumps({k:v for k,v in result.items() if k!='entries'}, indent=2))


if __name__ == '__main__':
    main()
