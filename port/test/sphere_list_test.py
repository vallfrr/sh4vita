"""Compare the direct sphere/polygon service with the supported GOG x86.

References and original-format synthetic resources exist only temporarily.
The BSP-chain oracle is separate: this covers its other, direct-list caller.
"""
import argparse
import ctypes as C
import hashlib
import os
from pathlib import Path
import random
import struct
import subprocess
import tempfile

from collision_chain_test import (ROOT, CPU, SIZE, TREE, QUERY, SP, STOP,
    references, scene, PE, Uc, UC_ARCH_X86, UC_MODE_32,
    UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EBX,
    UC_X86_REG_ESP, UC_X86_REG_EBP, UC_X86_REG_ESI, UC_X86_REG_EDI,
    UC_X86_REG_EIP, UC_X86_REG_FPCW, UC_X86_REG_FPSW, UC_X86_REG_FPTAG)


def source():
    text=references().replace('#include "x86rt.h"','#include "rt.h"',1)
    first=text.index('int test_native(CPU *c) {')
    last=text.index('\nvoid F_',first)
    adapter='''
int test_native(CPU *c) {
    uint8_t *const mb=g_mem;uint32_t sp=c->esp,query=R32(sp+12);
    Sh4CollisionView v={.data=g_mem,.size=32u<<20,.query_address=query,
        .exclude_begin=sp-0x10000,.exclude_end=sp+16};
    Sh4CollisionQuery q={.count=R32(0xed7938)};
    memcpy(q.sphere,mb+query,20);memcpy(q.work,mb+0xed7460,sizeof q.work);
    memcpy(q.polygon,mb+0xed7538,sizeof q.polygon);
    if(!sh4_collision_polygons(&v,R32(sp+4),(int32_t)R32(sp+8),&q)) return 0;
    memcpy(mb+query,q.sphere,20);memcpy(mb+0xed7460,q.work,sizeof q.work);
    memcpy(mb+0xed7538,q.polygon,sizeof q.polygon);W32(0xed7938,q.count);
    c->eax=0;c->esp+=4;return 1;
}
'''
    bridge='''
uint32_t g_mem_size=32u<<20;CPU *g_main_cpu;unsigned g_frame,test_failed;
void nat_collision_polygons(CPU *c);
void F_0041f570_orig(CPU *c) {F_0041f570(c);}
gfn rt_lookup(uint32_t fn) {
    return fn==0x420a70 ? F_00420a70 : fn==0x41ac30 ? F_0041ac30 : NULL;
}
void test_bridge(CPU *c) {g_main_cpu=c;nat_collision_polygons(c);}
void rt_log(const char *fmt,...) {
    char msg[1024];va_list ap;va_start(ap,fmt);vsnprintf(msg,sizeof msg,fmt,ap);va_end(ap);
    fputs(msg,stdout);
    if(strstr(msg,"failed=") && !strstr(msg,"failed=0 "))test_failed++;
}
void test_enable(void) {
#ifdef _WIN32
    _putenv("SH4_NATIVE_SPHERE_LIST=1");_putenv("SH4_SPHERE_LIST_VERIFY=1000");
#else
    setenv("SH4_NATIVE_SPHERE_LIST","1",1);setenv("SH4_SPHERE_LIST_VERIFY","1000",1);
#endif
}
'''
    return text[:first]+adapter+text[last:]+bridge


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--cc',default='gcc');ap.add_argument('--cases',type=int,default=2000)
    args=ap.parse_args()
    if args.cases<1:ap.error('--cases must be positive')
    game=ROOT/'game/sh4.exe'
    assert hashlib.sha256(game.read_bytes()).hexdigest()==\
        'cf7081a6c78dbe51f6dc2e1c9bd978d8486f1d7b59d7a3b88e027c9b39f21dd0'
    pe=PE(game);rnd=random.Random(0x41f570)
    with tempfile.TemporaryDirectory(prefix='sh4-sphere-list-',ignore_cleanup_errors=True) as tmp:
        src=Path(tmp)/'refs.c';src.write_text(source(),encoding='utf-8')
        dll=Path(tmp)/('refs.dll' if os.name=='nt' else 'refs.so')
        subprocess.run([args.cc,'-std=gnu11','-O2','-ffp-contract=off','-shared','-fPIC',
            '-I'+str(ROOT/'port/src/rt'),str(src),str(ROOT/'port/src/rt/collision_native.c'),
            str(ROOT/'port/src/rt/collision_list_bridge.c'),
            '-o',str(dll),'-lm'],check=True)
        lib=C.CDLL(str(dll));lib.test_enable();memory=C.create_string_buffer(SIZE)
        C.c_void_p.in_dll(lib,'g_mem').value=C.addressof(memory)
        lib.F_0041f570.argtypes=[C.POINTER(CPU)];lib.test_native.argtypes=[C.POINTER(CPU)]
        uc=Uc(UC_ARCH_X86,UC_MODE_32);uc.mem_map(0,SIZE)
        def write(p,data):
            uc.mem_write(p,data);C.memmove(C.addressof(memory)+p,data,len(data))
        for _,va,_,raw,n,_ in pe.sections:
            if n:write(va,pe.data[raw:raw+n])
        regs=[UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_EBX,
              UC_X86_REG_ESP,UC_X86_REG_EBP,UC_X86_REG_ESI,UC_X86_REG_EDI]
        hits=mutated=saturated=0
        for case in range(args.cases):
            tree,sphere=scene(rnd,case)
            if case%19==2:
                values=list(struct.unpack('<5f',sphere));values[4]=-values[4]
                sphere=struct.pack('<5f',*values)
            if case%19==3:
                values=list(struct.unpack('<5f',sphere));values[3]=[-0.,0.,1.][case%3]
                sphere=struct.pack('<5f',*values)
            count,offset=struct.unpack_from('<2I',tree,32)
            poly=TREE+32+offset
            if case%41==1:count=0xffffffff # original still initializes workspace
            data=bytearray(rnd.randbytes(0x600))
            struct.pack_into('<I',data,0x100,0x420a70)
            struct.pack_into('<I',data,0x538,[0,1,255,256][case%4] if case%23==0 else 0)
            write(TREE,tree);write(QUERY,sphere);write(0xed7400,bytes(data))
            stop=0x420c33
            call_args=struct.pack('<4I',stop,poly,count,QUERY)
            write(SP-0x10000,bytes(0x10100));write(SP,call_args)
            initial=CPU(eax=0x1234,ebx=0x12345678,esi=0x23456789,edi=0x3456789a,
                        ebp=0x456789ab,esp=SP,fcw=[0x23f,0x27f,0x33f,0x37f][case%4],ftop=case%8)
            for reg,(name,_) in zip(regs,CPU._fields_[:8]):uc.reg_write(reg,getattr(initial,name))
            uc.reg_write(UC_X86_REG_FPCW,initial.fcw)
            uc.reg_write(UC_X86_REG_FPSW,initial.ftop<<11);uc.reg_write(UC_X86_REG_FPTAG,0xffff)
            uc.emu_start(0x41f570,stop,count=5000000)
            assert uc.reg_read(UC_X86_REG_EIP)==stop,(case,'x86 limit')
            for label in ('generated','native','bridge'):
                C.memmove(C.addressof(memory)+0xed7400,bytes(data),len(data))
                C.memmove(C.addressof(memory)+QUERY,sphere,20)
                C.memset(C.addressof(memory)+SP-0x10000,0,0x10100)
                C.memmove(C.addressof(memory)+SP,call_args,len(call_args))
                cpu=CPU.from_buffer_copy(bytes(initial))
                if label=='generated':lib.F_0041f570(C.byref(cpu))
                elif label=='bridge':lib.test_bridge(C.byref(cpu))
                else:assert lib.test_native(C.byref(cpu))==1,(case,'unexpected refusal')
                assert not C.c_uint.in_dll(lib,'test_failed').value,(case,'production verification')
                for p,n in [(QUERY,20),(0xed7400,0x600),(TREE,len(tree))]:
                    a=bytes(uc.mem_read(p,n));b=C.string_at(C.addressof(memory)+p,n)
                    if a!=b:
                        off=next(i for i in range(n) if a[i]!=b[i])
                        raise AssertionError((case,label,hex(p+off),a[off:off+8].hex(),b[off:off+8].hex()))
                for reg,name in [(UC_X86_REG_EAX,'eax'),(UC_X86_REG_ESP,'esp'),
                    (UC_X86_REG_EBX,'ebx'),(UC_X86_REG_ESI,'esi'),(UC_X86_REG_EDI,'edi'),(UC_X86_REG_EBP,'ebp')]:
                    assert uc.reg_read(reg)==getattr(cpu,name),(case,label,name)
                assert cpu.ftop==initial.ftop and cpu.fcw==initial.fcw,(case,label,'FPU ABI')
            result=struct.unpack('<I',uc.mem_read(0xed7938,4))[0]
            hits+=result;saturated+=result==256;mutated+=bytes(uc.mem_read(QUERY,20))!=sphere
        assert hits and saturated and mutated
        print(f'{args.cases} direct sphere-list queries match GOG x86/generated/native: '
              f'{hits} ordered candidates, {saturated} saturated lists, {mutated} sphere writes; '
              'full collision globals/source geometry/production bridge compared')
        for fault in ('range','count','query-alias','global-alias','stack-alias','nan','extreme','vertices'):
            tree,sphere=scene(rnd,0);poly=TREE+0x4000;count=1
            if fault=='range':poly=SIZE-8
            elif fault=='count':count=1000001
            elif fault=='query-alias':poly=QUERY
            elif fault=='global-alias':poly=0xed7400
            elif fault=='stack-alias':poly=SP
            elif fault=='nan':sphere=struct.pack('<I4f',0x7fc12345,2.,.25,-2.,1.)
            elif fault=='extreme':sphere=struct.pack('<5f',1e20,2.,.25,-2.,1.)
            elif fault=='vertices':
                tree=bytearray(tree);tree[0x4000+12]=1;tree=bytes(tree)
            data=bytearray(rnd.randbytes(0x600));struct.pack_into('<I',data,0x538,0)
            C.memmove(C.addressof(memory)+TREE,tree,len(tree))
            C.memmove(C.addressof(memory)+QUERY,sphere,20)
            C.memmove(C.addressof(memory)+0xed7400,bytes(data),len(data))
            C.memmove(C.addressof(memory)+SP,struct.pack('<4I',0x420c33,poly,count,QUERY),16)
            cpu=CPU(esp=SP,fcw=0x23f);initial=bytes(cpu)
            assert lib.test_native(C.byref(cpu))==0,(fault,'accepted')
            assert bytes(cpu)==initial,(fault,'CPU changed')
            for p,value in [(TREE,tree),(QUERY,sphere),(0xed7400,bytes(data))]:
                assert C.string_at(C.addressof(memory)+p,len(value))==value,(fault,'partial write')
        print('8 unsupported list views refuse without guest/CPU writes')
        if os.name=='nt':
            import _ctypes
            _ctypes.FreeLibrary(lib._handle)


if __name__=='__main__':main()
