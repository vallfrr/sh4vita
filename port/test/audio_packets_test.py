"""Compare the actual GOG packet loop, generated reference and native prefix.

Uses only the locally owned executable. Checks complete PCM/source/canaries,
the loop's live stack and every CPU/FPU byte; no extracted assets are saved.
"""
import argparse
import ast
import ctypes as C
import os
from pathlib import Path
import random
import re
import struct
import subprocess
import tempfile

from adpcm_test import (ROOT,CPU,PE,Uc,UC_ARCH_X86,UC_MODE_32,SIZE,SP,reference,
    UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_EBX,
    UC_X86_REG_ESP,UC_X86_REG_EBP,UC_X86_REG_ESI,UC_X86_REG_EDI,
    UC_X86_REG_EIP,UC_X86_REG_FPCW,UC_X86_REG_FPSW,UC_X86_REG_FPTAG)


def source():
    text=next(p.read_text() for p in (ROOT/'port/gen').glob('x86_f*.c')
              if 'L_0040ab20:;' in p.read_text())
    loop=text[text.index('L_0040ab20:;'):text.index('L_0040abf5:;')]
    # Always reconstruct the preserved loop, even after regeneration.
    loop=re.sub(r'(L_0040ab20:;\n)  (?:if\(audio_packets_enabled\) )?\{\n    extern uint32_t rt_adpcm_packet_prefix.*?\n  \}\n',
                r'\1',loop,flags=re.S)
    tree=ast.parse((ROOT/'recomp/x86rec.py').read_text())
    node=next(n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='optimize_audio_packets')
    scope={};exec(compile(ast.Module(body=[node],type_ignores=[]),'transform','exec'),scope)
    prefix='''
uint8_t *const mb=g_mem;
uint32_t eax=c->eax,ecx=c->ecx,edx=c->edx,ebx=c->ebx,esp=c->esp,ebp=c->ebp,esi=c->esi,edi=c->edi;
uint32_t cf=0,zf=0,sf=0,of=0,pf=0,fa=0,fb=0,fr=0;
'''
    suffix='''
c->eax=eax;c->ecx=ecx;c->edx=edx;c->ebx=ebx;c->esp=esp;c->ebp=ebp;c->esi=esi;c->edi=edi;
}
'''
    return (reference()+'\nvoid test_original(CPU *c) {\n'+prefix+loop+suffix+
            '\nvoid test_native(CPU *c) {\n'+prefix+scope['optimize_audio_packets'](loop).replace(
                'if(skipped) {', 'test_skipped+=skipped; if(skipped) {')+suffix)


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--cc',default='gcc');ap.add_argument('--cases',type=int,default=2000)
    args=ap.parse_args();rnd=random.Random(0x40ab20)
    game=ROOT/'game/sh4.exe'
    import hashlib
    assert hashlib.sha256(game.read_bytes()).hexdigest()=='cf7081a6c78dbe51f6dc2e1c9bd978d8486f1d7b59d7a3b88e027c9b39f21dd0'
    head='''
#include "rt.h"
#include "audio_adpcm.h"
#include <stdlib.h>
uint8_t *g_mem;uint32_t g_mem_size=32u<<20;CPU *g_main_cpu;
unsigned g_frame,test_skipped,test_failed,test_verified;
void F_0040a880(CPU *c);
void F_0040a880_orig(CPU *c) {F_0040a880(c);}
void rt_log(const char *fmt,...) {
char msg[512];va_list ap;va_start(ap,fmt);vsnprintf(msg,sizeof msg,fmt,ap);va_end(ap);
if(strstr(msg,"verified=")) {test_verified++;if(!strstr(msg,"failed=0 "))test_failed++;}
}
void test_enable(void) {
#ifdef _WIN32
_putenv("SH4_NATIVE_AUDIO_PACKETS=1");
_putenv("SH4_AUDIO_PACKETS_VERIFY=8");
#else
setenv("SH4_NATIVE_AUDIO_PACKETS","1",1);
setenv("SH4_AUDIO_PACKETS_VERIFY","8",1);
#endif
}
'''
    pe=PE(game);regs=[UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_EBX,
        UC_X86_REG_ESP,UC_X86_REG_EBP,UC_X86_REG_ESI,UC_X86_REG_EDI]
    with tempfile.TemporaryDirectory(prefix='sh4-packets-',ignore_cleanup_errors=True) as tmp:
        srcfile=Path(tmp)/'test.c';srcfile.write_text(head+source())
        dll=Path(tmp)/('test.dll' if os.name=='nt' else 'test.so')
        subprocess.run([args.cc,'-O2','-shared','-fPIC','-I'+str(ROOT/'port/src/rt'),str(srcfile),
            str(ROOT/'port/src/rt/audio_adpcm.c'),str(ROOT/'port/src/rt/audio_adpcm_bridge.c'),
            '-o',str(dll),'-lm'],check=True)
        lib=C.CDLL(str(dll));lib.test_enable()
        memory=C.create_string_buffer(SIZE);C.c_void_p.in_dll(lib,'g_mem').value=C.addressof(memory)
        lib.test_original.argtypes=lib.test_native.argtypes=[C.POINTER(CPU)]
        lib.rt_adpcm_packet_prefix.argtypes=[C.POINTER(CPU)]+[C.c_uint32]*5
        lib.rt_adpcm_packet_prefix.restype=C.c_uint32
        uc=Uc(UC_ARCH_X86,UC_MODE_32);uc.mem_map(0,SIZE)
        for _,va,_,raw,n,_ in pe.sections:
            if n:uc.mem_write(va,pe.data[raw:raw+n])
        stereo=compact=aliases=0
        for case in range(args.cases):
            obj=0x400000;part=(case%4)*0x1ac0c8;bank=case%2;mode=[0,1,2,0xffffffff][case%4]
            count=([1,2,4096,4096][case] if case<4 else rnd.randrange(1,65))
            stride=64 if mode else 32;position=rnd.randrange(64);written=rnd.randrange(64)
            dst=obj+part+0x12c0d0+written;src=obj+part+0x1ac0d0+bank*0x96000+position
            if case%31==0:dst=src-5;written=dst-obj-part-0x12c0d0;aliases+=1
            lo=min(dst,src)-64;hi=max(dst+count*stride,src+count*26)+64
            data=rnd.randbytes(hi-lo);stack=bytearray(rnd.randbytes(0x200))
            struct.pack_into('<I',stack,0x100+0x10,count*26)
            struct.pack_into('<I',stack,0x100+0x14,dst+2)
            struct.pack_into('<I',stack,0x100+0x68,dst)
            meta=[(obj,struct.pack('<I',mode)),(obj+part+0x12c09c,struct.pack('<I',bank))]
            initial=CPU.from_buffer_copy(rnd.randbytes(C.sizeof(CPU)))
            initial.esi=obj;initial.ebp=part;initial.esp=SP;initial.ebx=position;initial.edi=written
            initial.df=0;initial.fcw=0x23f;initial.ftop=case%8;initial.fsw=0x4000
            uc.mem_write(lo,data);uc.mem_write(SP-0x100,bytes(stack))
            for addr,value in meta:uc.mem_write(addr,value)
            for reg,(name,_) in zip(regs,CPU._fields_[:8]):uc.reg_write(reg,getattr(initial,name))
            uc.reg_write(UC_X86_REG_FPCW,initial.fcw);uc.reg_write(UC_X86_REG_FPSW,initial.fsw|(initial.ftop<<11))
            uc.reg_write(UC_X86_REG_FPTAG,0xffff)
            uc.emu_start(0x40ab20,0x40abf5,count=100000000)
            assert uc.reg_read(UC_X86_REG_EIP)==0x40abf5,(case,'instruction limit')
            expected=bytes(uc.mem_read(lo,hi-lo));expected_stack=bytes(uc.mem_read(SP,0x100))
            cpu_reference=None
            for name in ('test_original','test_native'):
                C.memmove(C.addressof(memory)+lo,data,len(data))
                C.memmove(C.addressof(memory)+SP-0x100,bytes(stack),len(stack))
                for addr,value in meta:C.memmove(C.addressof(memory)+addr,value,len(value))
                cpu=CPU.from_buffer_copy(bytes(initial));C.c_void_p.in_dll(lib,'g_main_cpu').value=C.addressof(cpu)
                C.c_uint.in_dll(lib,'test_skipped').value=0
                getattr(lib,name)(C.byref(cpu))
                skipped=C.c_uint.in_dll(lib,'test_skipped').value
                if name=='test_native':
                    if case%31:
                        assert skipped==(count-1)*26,(case,'native prefix not exercised',skipped)
                    else:
                        # After original aliasing iterations, the remaining
                        # ranges can become disjoint and eligible again.
                        assert skipped%26==0 and skipped<=(count-1)*26,(case,'prefix length')
                assert C.string_at(C.addressof(memory)+lo,hi-lo)==expected,(case,name,'PCM/source/canaries')
                assert C.string_at(C.addressof(memory)+SP,0x100)==expected_stack,(case,name,'live stack')
                for reg,(field,_) in zip(regs,CPU._fields_[:8]):
                    assert getattr(cpu,field)==uc.reg_read(reg),(case,name,field)
                if cpu_reference is None:cpu_reference=bytes(cpu)
                else:assert bytes(cpu)==cpu_reference,(case,'complete CPU/FPU')
            if mode:stereo+=1
            else:compact+=1
        # Invalid or unaudited inputs must refuse before touching guest state.
        obj=0x400000;dst=obj+0x12c0d0
        for fault in ('short','partial','oversize','bank','right','written','source','low-stack',
                      'high-stack','object','part','other-cpu','overlap','stack-overlap'):
            sp=SP;part=position=written=0;target=dst;size=52;bank=0;right=target+2
            if fault=='short':size=26
            elif fault=='partial':size=53
            elif fault=='oversize':size=0x1a000+26
            elif fault=='bank':bank=2
            elif fault=='right':right+=1
            elif fault=='written':written=1
            elif fault=='source':position=0xffffffff
            elif fault=='low-stack':sp=0xff
            elif fault=='high-stack':sp=SIZE-0x6f
            elif fault=='part':part=0xffffffff
            elif fault=='overlap':
                target=obj+0x1ac0d0;written=target-dst;right=target+2
            elif fault=='stack-overlap':sp=target
            C.memmove(C.addressof(memory)+obj,struct.pack('<I',1),4)
            C.memmove(C.addressof(memory)+obj+0x12c09c,struct.pack('<I',bank),4)
            if 0x100<=sp<=SIZE-0x70:
                C.memmove(C.addressof(memory)+sp+0x10,struct.pack('<II',size,right),8)
                C.memmove(C.addressof(memory)+sp+0x68,struct.pack('<I',target),4)
            cpu=CPU(esp=sp);original_cpu=bytes(cpu)
            C.c_void_p.in_dll(lib,'g_main_cpu').value=0 if fault=='other-cpu' else C.addressof(cpu)
            before=C.string_at(C.addressof(memory),SIZE)
            result=lib.rt_adpcm_packet_prefix(C.byref(cpu),SIZE-3 if fault=='object' else obj,
                                              part,sp,position,written)
            assert result==0 and bytes(cpu)==original_cpu and C.string_at(C.addressof(memory),SIZE)==before,(fault,'transactional guard')
        print(f'{args.cases} actual GOG/generated/native packet-loop comparisons pass; '
              f'{stereo} stereo, {compact} compact, {aliases} alias fallbacks; '
              'PCM/source/canaries/live stack/full CPU/FPU match, including 4096-packet batches; 14 guards pass')
        assert not C.c_uint.in_dll(lib,'test_failed').value,'production batch verification failed'
        assert C.c_uint.in_dll(lib,'test_verified').value==8,'production verification path not exercised'
        if os.name=='nt':
            import _ctypes
            _ctypes.FreeLibrary(lib._handle)


if __name__=='__main__':main()
