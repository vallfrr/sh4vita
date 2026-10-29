"""Compare isolated native passes to the retained bounds/UV implementations."""
import argparse
import ast
import ctypes as C
import hashlib
import os
from pathlib import Path
import random
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'recomp'))
from pe import PE

class CPU(C.Structure):
    _fields_ = [(s, C.c_uint32) for s in
        ('eax','ecx','edx','ebx','esp','ebp','esi','edi','df','eflags_ext','fs_base')] + [
        ('st', C.c_double * 8)] + [(s,C.c_uint32) for s in ('ftop','fsw','fcw')]

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--cc',default='gcc')
    ap.add_argument('--cases',type=int,default=2000)
    ap.add_argument('--mode',choices=['1','bounds','uv'],default='1')
    args=ap.parse_args()
    tree=ast.parse((ROOT/'recomp/x86rec.py').read_text())
    fn=next(n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='optimize_model_bbox')
    bounds=next(ast.literal_eval(n.value) for n in fn.body if isinstance(n,ast.Assign)
                and isinstance(n.targets[0],ast.Name) and n.targets[0].id=='native')
    uv=(ROOT/'recomp/model_uv_native.inc').read_text()
    fields=['eax','ecx','edx','ebx','esp','ebp','esi','edi']
    reload=','.join(f'{x}=c->{x}' for x in fields)
    spill=''.join(f'c->{x}={x};' for x in fields)
    head='''#include "rt.h"
#include <stdlib.h>
uint8_t *g_mem;
uint32_t g_mem_size;
void test_enable(void) {
#ifdef _WIN32
 _putenv("SH4_NATIVE_VERTICES=1");
#else
 setenv("SH4_NATIVE_VERTICES","1",1);
#endif
}
'''
    refs=''
    for name,body in [('ref_bounds',bounds),('ref_uv',uv)]:
        refs+=f'void {name}(CPU *c) {{ uint8_t *const mb=g_mem; uint32_t {reload};\n'
        refs+='uint32_t cf=0,zf=0,sf=0,of=0,pf=0,fa=0,fb=0,fr=0;\n'+body+'\n'+spill+'}\n'
    with tempfile.TemporaryDirectory(prefix='sh4-model-passes-',ignore_cleanup_errors=True) as tmp:
        source=Path(tmp)/'refs.c';source.write_text(head.replace('SH4_NATIVE_VERTICES=1','SH4_NATIVE_VERTICES='+args.mode)+refs)
        library=Path(tmp)/('passes.dll' if os.name=='nt' else 'passes.so')
        subprocess.run([args.cc,'-std=gnu11','-O2','-shared','-fPIC',
            *(['-Wl,--export-all-symbols'] if os.name=='nt' else []),
            '-I'+str(ROOT/'port/src/rt'),str(source),str(ROOT/'port/src/rt/model.c'),
            '-o',str(library)],check=True)
        lib=C.CDLL(str(library));lib.test_enable()
        C.c_uint32.in_dll(lib,'g_mem_size').value=32<<20
        memories=[C.create_string_buffer(32<<20) for _ in range(2)]
        global_mem=C.c_void_p.in_dll(lib,'g_mem')
        for name in ['ref_bounds','ref_uv','nat_model_bounds','nat_model_uv']:
            getattr(lib,name).argtypes=[C.POINTER(CPU)]
        game=ROOT/'game/sh4.exe'
        assert hashlib.sha256(game.read_bytes()).hexdigest()==\
            'cf7081a6c78dbe51f6dc2e1c9bd978d8486f1d7b59d7a3b88e027c9b39f21dd0'
        pe=PE(game)
        constants=pe.read(0x5b7b08,24)
        for mem in memories:C.memmove(C.addressof(mem)+0x5b7b08,constants,24)
        rnd=random.Random(0x4290c0)
        words=[0,0x80000000,0x3e75c28f,0x3e851eb8,0x3e800000,0x3f800000,0xbf800000,
               0x7f800000,0xff800000,0x7fc00000,0x7fc12345,0xffc54321,1,0x80000001,0x7f7fffff]
        stack=0x30100;src=0x1400000;dst=0xf529b0;desc=0x10000
        count=0
        for case in range(args.cases):
            n=[0,1,2,17,64,257][case%6]
            data=bytearray(rnd.randbytes(n*64))
            for i in range(n):
                vals=[rnd.choice(words) if case%3 else struct.unpack('<I',struct.pack('<f',rnd.uniform(-2,2)))[0] for _ in range(2)]
                struct.pack_into('<2I',data,i*64+0x18,*vals)
            initial_stack=bytearray(rnd.randbytes(128))
            # Parameter offsets relative to the stack region starting at ESP-16.
            for off in [0x30,0x28,0x1c,0x24,0x10,0x40,0x44,0x34]:
                struct.pack_into('<I',initial_stack,16+off,rnd.choice(words))
            cpu=CPU.from_buffer_copy(rnd.randbytes(C.sizeof(CPU)))
            cpu.ecx=src;cpu.edx=n;cpu.esp=stack;cpu.esi=desc;cpu.edi=dst+0x1c
            for name in (['bounds','uv'] if args.mode=='1' else [args.mode]):
                for mem in memories:
                    C.memmove(C.addressof(mem)+src,bytes(data),len(data))
                    C.memset(C.addressof(mem)+dst,0xa5,len(data))
                    C.memmove(C.addressof(mem)+stack-16,bytes(initial_stack),len(initial_stack))
                    C.memmove(C.addressof(mem)+desc+0x44,struct.pack('<I',n),4)
                a=CPU.from_buffer_copy(bytes(cpu));b=CPU.from_buffer_copy(bytes(cpu))
                global_mem.value=C.addressof(memories[0]);getattr(lib,'ref_'+name)(C.byref(a))
                global_mem.value=C.addressof(memories[1]);ok=getattr(lib,'nat_model_'+name)(C.byref(b))
                assert ok==1,(case,name,'declined')
                assert bytes(a)==bytes(b),(case,name,'CPU')
                for address,size in [(stack-16,128),(src,len(data)),(dst,len(data)),(desc+0x44,4),(0x5b7b08,24)]:
                    assert C.string_at(C.addressof(memories[0])+address,size)==C.string_at(C.addressof(memories[1])+address,size),(case,name,hex(address))
                count+=1
        # Overlapping source/output/stack and invalid spans retain the old path.
        for name in ['bounds','uv']:
            if args.mode!='1' and name!=args.mode: continue
            for mode in range(4):
                b=CPU.from_buffer_copy(bytes(cpu));b.edx=2;b.ecx=src;b.edi=dst+0x1c
                C.memmove(C.addressof(memories[1])+desc+0x44,struct.pack('<I',2),4)
                if mode==0:b.ecx=stack
                elif mode==1:b.ecx=(32<<20)-4
                elif mode==2:b.esp=8
                elif name=='uv':b.edi=src+0x1c
                else:b.edx=0xffffffff
                saved=bytes(b)
                assert getattr(lib,'nat_model_'+name)(C.byref(b))==0,(name,mode)
                assert saved==bytes(b)
        print(f'{count} retained/native comparisons passed: complete CPU, stack, all vertex bytes, '
              'signed zero, infinities, NaN payloads, threshold cases, empty passes and alias/range fallbacks.')
        if os.name=='nt':
            import _ctypes
            _ctypes.FreeLibrary(lib._handle)

if __name__=='__main__':main()
