"""Bit-exact GOG audio decoder oracle, including the production CPU bridge.

The player-owned executable and generated reference are used locally only.
Check PCM bytes, integer registers, FPU/CPU preservation, aliases and guards.
"""
import argparse
import ctypes as C
import hashlib
import os
from pathlib import Path
import random
import re
import struct
import subprocess
import tempfile

from collision_chain_test import (ROOT,CPU,PE,Uc,UC_ARCH_X86,UC_MODE_32,
    UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_EBX,
    UC_X86_REG_ESP,UC_X86_REG_EBP,UC_X86_REG_ESI,UC_X86_REG_EDI,
    UC_X86_REG_EIP,UC_X86_REG_FPCW,UC_X86_REG_FPSW,UC_X86_REG_FPTAG)

SIZE,POOL,SP=32<<20,0x100000,0x2fff00


def reference():
    for p in (ROOT/'port/gen').glob('x86_f*.c'):
        text=p.read_text(errors='replace')
        m=(re.search(r'^void F_0040a880_orig\(CPU \*c\) \{',text,re.M) or
           re.search(r'^void F_0040a880\(CPU \*c\) \{',text,re.M))
        if m:
            end=text.index('\nvoid F_',m.end())
            return text[m.start():end].replace('F_0040a880_orig(','F_0040a880(',1)
    raise RuntimeError('Regenerate port/gen: audio decoder reference missing')


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--cc',default='gcc');ap.add_argument('--cases',type=int,default=2000)
    args=ap.parse_args()
    if args.cases<1500:ap.error('--cases must exercise the fast path after 1000 verified blocks')
    game=ROOT/'game/sh4.exe'
    assert hashlib.sha256(game.read_bytes()).hexdigest()==\
        'cf7081a6c78dbe51f6dc2e1c9bd978d8486f1d7b59d7a3b88e027c9b39f21dd0'
    head='''
#include "rt.h"
#include "audio_adpcm.h"
#include <stdlib.h>
uint8_t *g_mem;uint32_t g_mem_size=32u<<20;CPU *g_main_cpu;
unsigned g_frame,test_failed,test_original_calls,test_stub;
void F_0040a880(CPU *c);
void F_0040a880_orig(CPU *c) {++test_original_calls;if(!test_stub)F_0040a880(c);}
void nat_audio_adpcm(CPU *c);
void test_bridge(CPU *c) {g_main_cpu=c;nat_audio_adpcm(c);}
void rt_log(const char *fmt,...) {
    char msg[1024];va_list ap;va_start(ap,fmt);vsnprintf(msg,sizeof msg,fmt,ap);va_end(ap);
    fputs(msg,stdout);
    if(strstr(msg,"failed=") && !strstr(msg,"failed=0 "))test_failed++;
}
void test_enable(void) {
#ifdef _WIN32
    _putenv("SH4_NATIVE_ADPCM=1");_putenv("SH4_ADPCM_VERIFY=1000");
#else
    setenv("SH4_NATIVE_ADPCM","1",1);setenv("SH4_ADPCM_VERIFY","1000",1);
#endif
}
'''
    pe=PE(game);rnd=random.Random(0x40a880)
    with tempfile.TemporaryDirectory(prefix='sh4-adpcm-',ignore_cleanup_errors=True) as tmp:
        srcfile=Path(tmp)/'reference.c';srcfile.write_text(head+reference(),encoding='utf-8')
        dll=Path(tmp)/('test.dll' if os.name=='nt' else 'test.so')
        subprocess.run([args.cc,'-std=gnu11','-O2','-shared','-fPIC',
            '-I'+str(ROOT/'port/src/rt'),str(srcfile),str(ROOT/'port/src/rt/audio_adpcm.c'),
            str(ROOT/'port/src/rt/audio_adpcm_bridge.c'),'-o',str(dll),'-lm'],check=True)
        lib=C.CDLL(str(dll));lib.test_enable()
        memory=C.create_string_buffer(SIZE);C.c_void_p.in_dll(lib,'g_mem').value=C.addressof(memory)
        lib.F_0040a880.argtypes=lib.test_bridge.argtypes=[C.POINTER(CPU)]
        lib.sh4_adpcm_block.argtypes=[C.c_void_p,C.c_void_p,C.POINTER(C.c_uint32)]
        lib.sh4_adpcm_block.restype=C.c_uint32
        uc=Uc(UC_ARCH_X86,UC_MODE_32);uc.mem_map(0,SIZE)
        for _,va,_,raw,n,_ in pe.sections:
            if n:uc.mem_write(va,pe.data[raw:raw+n])
        regs=[UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_EBX,
              UC_X86_REG_ESP,UC_X86_REG_EBP,UC_X86_REG_ESI,UC_X86_REG_EDI]
        aliases=wrapped=0
        for case in range(args.cases):
            data=bytearray(rnd.randbytes(256));src=POOL+64+case%7;dst=POOL+128+case%5
            if case%19==0:dst=src+[-24,-3,0,1,12][case%5];aliases+=1
            overlapping=dst<src+13 and src<dst+32
            h0=[-32768,-1,0,1,32767,rnd.randrange(-32768,32768)][case%6]
            h1=rnd.randrange(-32768,32768)
            block=struct.pack('<hhH',h0,h1,case&65535)+rnd.randbytes(7)
            if case%17==0:block=block[:6]+bytes([case&255])*7
            data[src-POOL:src-POOL+13]=block
            stop=[0x40aada,0x40ab45,0x40ab92,0x18000][case%4]
            call_args=struct.pack('<3I',stop,dst,src)
            initial=CPU.from_buffer_copy(rnd.randbytes(C.sizeof(CPU)))
            initial.esp=SP;initial.df=0;initial.ftop=case%8;initial.fsw=0x4000
            initial.fcw=[0x23f,0x27f,0x33f,0x37f][case%4]
            uc.mem_write(POOL,bytes(data));uc.mem_write(SP-0x100,bytes(0x140));uc.mem_write(SP,call_args)
            for reg,(name,_) in zip(regs,CPU._fields_[:8]):uc.reg_write(reg,getattr(initial,name))
            uc.reg_write(UC_X86_REG_FPCW,initial.fcw)
            uc.reg_write(UC_X86_REG_FPSW,initial.fsw|(initial.ftop<<11));uc.reg_write(UC_X86_REG_FPTAG,0xffff)
            uc.emu_start(0x40a880,stop,count=20000)
            assert uc.reg_read(UC_X86_REG_EIP)==stop,(case,'original instruction limit')
            expected_pool=bytes(uc.mem_read(POOL,len(data)))
            generated=None
            for label in ('generated','native','bridge'):
                C.memmove(C.addressof(memory)+POOL,bytes(data),len(data))
                C.memset(C.addressof(memory)+SP-0x100,0,0x140)
                C.memmove(C.addressof(memory)+SP,call_args,len(call_args))
                cpu=CPU.from_buffer_copy(bytes(initial))
                if label=='generated' or label=='native' and overlapping:
                    lib.F_0040a880(C.byref(cpu))
                elif label=='native':
                    pred=C.c_uint32();value=lib.sh4_adpcm_block(C.addressof(memory)+dst,C.addressof(memory)+src,C.byref(pred))
                    cpu.eax=src+13;cpu.ecx=value;cpu.edx=(pred.value&0xffff0000)|(value&65535);cpu.esp+=12
                else:lib.test_bridge(C.byref(cpu))
                actual=C.string_at(C.addressof(memory)+POOL,len(data))
                assert actual==expected_pool,(case,label,'PCM/canary/source mismatch')
                for reg,(name,_) in zip(regs,CPU._fields_[:8]):
                    assert getattr(cpu,name)==uc.reg_read(reg),(case,label,name,hex(getattr(cpu,name)),hex(uc.reg_read(reg)))
                if label=='generated':generated=bytes(cpu)
                else:assert bytes(cpu)==generated,(case,label,'full CPU/FPU mismatch')
                assert not C.c_uint.in_dll(lib,'test_failed').value,(case,'production verification')
            wrapped+=1
        assert C.c_uint.in_dll(lib,'test_original_calls').value<args.cases,'fast path not exercised'
        C.c_uint.in_dll(lib,'test_stub').value=1
        for fault in ('destination','source','overlap','dst-stack','src-stack','caller','stack'):
            src=POOL+64;dst=POOL+128;stop=0x40aada;sp=SP
            if fault=='destination':dst=SIZE-31
            elif fault=='source':src=SIZE-12
            elif fault=='overlap':dst=src
            elif fault=='dst-stack':dst=SP-16
            elif fault=='src-stack':src=SP-16
            elif fault=='caller':stop=0x18000
            elif fault=='stack':sp=SIZE-4
            C.memmove(C.addressof(memory)+SP,struct.pack('<3I',stop,dst,src),12)
            cpu=CPU(esp=sp);before=bytes(cpu);n=C.c_uint.in_dll(lib,'test_original_calls').value
            lib.test_bridge(C.byref(cpu))
            assert bytes(cpu)==before and C.c_uint.in_dll(lib,'test_original_calls').value==n+1,(fault,'guard')
        print(f'{wrapped} GOG x86/generated/native/production audio block comparisons pass; '
              f'{aliases} alias cases use the original ordering, full CPU/FPU and PCM/canaries compared; 7 guards pass')
        if os.name=='nt':
            import _ctypes
            _ctypes.FreeLibrary(lib._handle)


if __name__=='__main__':main()
