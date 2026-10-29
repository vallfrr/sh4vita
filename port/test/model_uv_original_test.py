"""Compare both native UV passes to the original GOG x86 block, not each other."""
import argparse
import ctypes as C
import hashlib
import os
from pathlib import Path
import random
import struct
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'recomp'))
from pe import PE
from model_vertices_test import CPU
from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE
from unicorn.x86_const import (UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EDX,
    UC_X86_REG_EBX,UC_X86_REG_ESP,UC_X86_REG_EBP,UC_X86_REG_ESI,UC_X86_REG_EDI,
    UC_X86_REG_EIP,UC_X86_REG_FPCW,UC_X86_REG_FPSW,UC_X86_REG_FPTAG)


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--cc',default='gcc');ap.add_argument('--cases',type=int,default=20000)
    ap.add_argument('--identity',action='store_true',help='Enable exact quarter-scale identity experiment')
    ap.add_argument('--prepared',action='store_true',help='Compare CPU-independent prepared UV arithmetic as well')
    ap.add_argument('--prefetch',action='store_true',help='Use the prefetching variant of the prepared pass')
    ap.add_argument('--bulk',action='store_true',help='Use the bulk-copy variant of the prepared pass')
    args=ap.parse_args()
    game=ROOT/'game/sh4.exe'
    assert hashlib.sha256(game.read_bytes()).hexdigest()==\
        'cf7081a6c78dbe51f6dc2e1c9bd978d8486f1d7b59d7a3b88e027c9b39f21dd0'
    pe=PE(game)
    fields=['eax','ecx','edx','ebx','esp','ebp','esi','edi']
    head='''#include "rt.h"
#include <stdlib.h>
uint8_t *g_mem;
uint32_t g_mem_size=32u<<20;
void test_enable(void) {
#ifdef _WIN32
 _putenv("SH4_NATIVE_VERTICES=1");
#else
 setenv("SH4_NATIVE_VERTICES","1",1);
#endif
}
void ref_uv(CPU *c) { uint8_t *const mb=g_mem;
'''
    head+='uint32_t '+','.join(f'{x}=c->{x}' for x in fields)+';\n'
    head+='uint32_t cf=0,zf=0,sf=0,of=0,pf=0,fa=0,fb=0,fr=0;\n'
    head+=(ROOT/'recomp/model_uv_native.inc').read_text()
    head+=''.join(f'c->{x}={x};' for x in fields)+'}\n'
    with tempfile.TemporaryDirectory(prefix='sh4-uv-x86-',ignore_cleanup_errors=True) as tmp:
        source=Path(tmp)/'uv.c';source.write_text(head)
        if args.prepared:
            text=source.read_text()+'''
#include "model_uv.h"
void prepared_uv(CPU *c) {
 uint8_t *mb=g_mem;uint32_t sp=c->esp,src=c->ecx,dst=c->edi-28,n=R32(c->esi+0x44);
 Sh4ModelUvParams p={.min_x=rt_f32(R32(sp+0x30)),.min_y=rt_f32(R32(sp+0x28)),
 .max_y=rt_f32(R32(sp+0x1c)),.range_x=rt_f32(R32(sp+0x24)),.range_y=rt_f32(R32(sp+0x10)),
 .lo=rt_f32(R32(0x5b7b18)),.hi=rt_f32(R32(0x5b7b14)),.wrap=rt_f32(R32(0x5b7b10)),.special=rt_f64(R64(0x5b7b08))};
 Sh4ModelUvPrepared uv[17];Sh4ModelUvStatus status;
 if(!sh4_model_uv_prepare(mb+src,n,&p,uv,&status)) abort();
 sh4_model_uv_apply(mb+src,mb+dst,n,uv,&status,rt_f32(R32(sp+0x40)),rt_f32(R32(sp+0x44)));
 if(n) {W32(sp-4,64);W32(sp-8,src+(n-1)*64);W32(sp-12,dst+(n-1)*64);W32(sp-16,0x429164);}
 rt_fcom(c,status.a,status.b);c->eax=c->edx=n;c->ecx=src+n*64;c->edi+=n*64;
 W32(sp+0x20,c->ecx);W32(sp+0x14,n);
}
'''
            if args.bulk:text=text.replace('sh4_model_uv_apply(', 'sh4_model_uv_apply_bulk(')
            elif args.prefetch:text=text.replace('sh4_model_uv_apply(', 'sh4_model_uv_apply_prefetched(')
            source.write_text(text)
        if args.identity:
            text=source.read_text()
            text=text.replace('_putenv("SH4_NATIVE_VERTICES=1");','_putenv("SH4_NATIVE_VERTICES=1"); _putenv("SH4_NATIVE_UV_IDENTITY=1");')
            text=text.replace('setenv("SH4_NATIVE_VERTICES","1",1);','setenv("SH4_NATIVE_VERTICES","1",1); setenv("SH4_NATIVE_UV_IDENTITY","1",1);')
            source.write_text(text)
        library=Path(tmp)/('uv.dll' if os.name=='nt' else 'uv.so')
        subprocess.run([args.cc,'-std=gnu11','-O2','-ffp-contract=off','-shared','-fPIC',
            *(['-Wl,--export-all-symbols'] if os.name=='nt' else []),
            '-I'+str(ROOT/'port/src/rt'),str(source),str(ROOT/'port/src/rt/model.c'),str(ROOT/'port/src/rt/model_uv.c'),
            '-o',str(library),'-lm'],check=True)
        lib=C.CDLL(str(library));lib.test_enable()
        mem=C.create_string_buffer(32<<20)
        C.c_void_p.in_dll(lib,'g_mem').value=C.addressof(mem)
        functions=['ref_uv','nat_model_uv']+(['prepared_uv'] if args.prepared else [])
        for f in functions+['nat_model_bounds']:getattr(lib,f).argtypes=[C.POINTER(CPU)]
        uc=Uc(UC_ARCH_X86,UC_MODE_32);uc.mem_map(0,32<<20)
        uc.mem_write(0x429158,pe.read(0x429158,0x42927d-0x429158))
        uc.mem_write(0x4290c0,pe.read(0x4290c0,0x42911e-0x4290c0))
        uc.mem_write(0x404a00,b'\xc3')
        uc.mem_write(0x5b7b08,pe.read(0x5b7b08,24))
        C.memmove(C.addressof(mem)+0x5b7b08,pe.read(0x5b7b08,24),24)
        def copy_call(u,pc,size,data):
            if pc!=0x404a00:return
            sp=u.reg_read(UC_X86_REG_ESP)
            ret,dst,src,n=struct.unpack('<4I',u.mem_read(sp,16))
            assert n==64
            u.mem_write(dst,bytes(u.mem_read(src,n)))
            u.reg_write(UC_X86_REG_EAX,dst)
            u.reg_write(UC_X86_REG_ESP,sp+4);u.reg_write(UC_X86_REG_EIP,ret)
        uc.hook_add(UC_HOOK_CODE,copy_call)
        regs=[UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_EBX,
              UC_X86_REG_ESP,UC_X86_REG_EBP,UC_X86_REG_ESI,UC_X86_REG_EDI]
        rnd=random.Random(0x429164);sp=0x30100;src=0x1400000;dst=0xf529b0;desc=0x10000
        compared=0
        for case in range(args.cases):
            n=[1,2,17][case%3]
            vertices=bytearray(rnd.randbytes(n*64))
            for i in range(n):struct.pack_into('<2f',vertices,i*64+24,rnd.randrange(-32,65)/64,rnd.randrange(-32,65)/64)
            if args.identity and case%5==0:
                for i in range(n):
                    # Finite float words across the complete exponent range,
                    # including subnormals, instead of only the usual UV grid.
                    for k in range(2):
                        word=rnd.getrandbits(32)
                        if word&0x7f800000==0x7f800000:word^=0x00800000
                        struct.pack_into('<I',vertices,i*64+24+k*4,word)
            stack=bytearray(rnd.randbytes(128))
            thresholds=[.245,.25,.255,.1,.5]+[struct.unpack('<f',struct.pack('<I',w))[0]
                for w in (0x3e75c28e,0x3e75c28f,0x3e75c290,0x3e851eb7,0x3e851eb8,0x3e851eb9)]
            params={0x30:[0.,.125,.5][case%3],0x28:.125,0x1c:.25 if case%2 else .375,
                    0x24:thresholds[case%len(thresholds)],0x10:thresholds[(case//3)%len(thresholds)] if case%7 else 0.,
                    0x40:.25,0x44:.5}
            for off,v in params.items():struct.pack_into('<f',stack,16+off,v)
            struct.pack_into('<I',stack,16+0x14,0)
            struct.pack_into('<I',stack,16+0x20,src)
            initial=CPU(eax=0,ecx=src,edx=n,ebx=0x12345678,esp=sp,
                        ebp=0x87654321,esi=desc,edi=dst+28,fcw=0x37f,ftop=case%8)
            uc.mem_write(src,bytes(vertices));uc.mem_write(dst,bytes([0xa5])*(n*64))
            uc.mem_write(sp-16,bytes(stack));uc.mem_write(desc+0x44,struct.pack('<I',n))
            for reg,name in zip(regs,fields):uc.reg_write(reg,getattr(initial,name))
            uc.reg_write(UC_X86_REG_FPCW,0x37f);uc.reg_write(UC_X86_REG_FPSW,initial.ftop<<11)
            uc.reg_write(UC_X86_REG_FPTAG,0xffff)
            uc.emu_start(0x429158,0x42927d,count=50000)
            assert uc.reg_read(UC_X86_REG_EIP)==0x42927d
            for func in functions:
                C.memmove(C.addressof(mem)+src,bytes(vertices),len(vertices))
                C.memset(C.addressof(mem)+dst,0xa5,n*64)
                C.memmove(C.addressof(mem)+sp-16,bytes(stack),len(stack))
                C.memmove(C.addressof(mem)+desc+0x44,struct.pack('<I',n),4)
                c=CPU.from_buffer_copy(bytes(initial));getattr(lib,func)(C.byref(c))
                for address,size in [(dst,n*64),(sp-16,len(stack)),(src,n*64)]:
                    a=bytes(uc.mem_read(address,size));b=C.string_at(C.addressof(mem)+address,size)
                    if a!=b:
                        off=next(i for i in range(size) if a[i]!=b[i])
                        raise AssertionError((case,func,hex(address+off),params,
                                              a[max(0,off-4):off+8].hex(),b[max(0,off-4):off+8].hex()))
                for reg,name in zip(regs,fields):
                    assert uc.reg_read(reg)==getattr(c,name),(case,func,name)
                assert (uc.reg_read(UC_X86_REG_FPSW)&0x4500)==(c.fsw&0x4500),(case,func,'FSW')
                assert ((uc.reg_read(UC_X86_REG_FPSW)>>11)&7)==c.ftop,(case,func,'TOP')
                compared+=1
            # Check the preceding bounds pass against its actual instructions,
            # including NaNs/infinities/signed zero in UV words. EAX and x87
            # condition codes are dead at the block exit (EAX is overwritten at
            # 0x42911e); compare its live registers and complete memory instead.
            bounded=bytearray(vertices)
            if case%4==0:
                struct.pack_into('<I',bounded,24,[0x7fc00001,0x7f800000,0xff800000,0x80000000][(case//4)%4])
            bounds_stack=bytearray(stack)
            for off,value in [(0x34,0.),(0x1c,0.),(0x30,2.),(0x28,2.)]:
                struct.pack_into('<f',bounds_stack,16+off,value)
            uc.mem_write(src,bytes(bounded));uc.mem_write(sp-16,bytes(bounds_stack))
            initial.edi=n
            for reg,name in zip(regs,fields):uc.reg_write(reg,getattr(initial,name))
            uc.reg_write(UC_X86_REG_FPSW,initial.ftop<<11);uc.reg_write(UC_X86_REG_FPTAG,0xffff)
            uc.emu_start(0x4290c0,0x42911e,count=50000)
            assert uc.reg_read(UC_X86_REG_EIP)==0x42911e
            C.memmove(C.addressof(mem)+src,bytes(bounded),len(bounded))
            C.memmove(C.addressof(mem)+sp-16,bytes(bounds_stack),len(bounds_stack))
            c=CPU.from_buffer_copy(bytes(initial));assert lib.nat_model_bounds(C.byref(c))==1
            for address,size in [(sp-16,len(stack)),(src,n*64)]:
                assert bytes(uc.mem_read(address,size))==C.string_at(C.addressof(mem)+address,size),(case,'bounds memory')
            for reg,name in zip(regs,fields):
                if name!='eax':assert uc.reg_read(reg)==getattr(c,name),(case,'bounds register',name)
        print(f'{compared} original-x86 UV comparisons passed: vertices, stack, registers and x87 comparisons')
        print(f'{args.cases} original-x86 bounds comparisons passed: complete memory and live registers')


if __name__=='__main__':main()
