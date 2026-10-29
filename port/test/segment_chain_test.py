"""Compare the complete polygon/segment filter to the local GOG x86 oracle.

No generated code or game assets are distributed. This first characterizes the
ordered collector and both nearest-face callbacks before replacing this chain.
"""
import argparse
import ctypes as C
import hashlib
import math
import os
from pathlib import Path
import random
import re
import struct
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'recomp'))
from pe import PE
from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE
from unicorn.x86_const import (UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EDX,
    UC_X86_REG_EBX,UC_X86_REG_ESP,UC_X86_REG_EBP,UC_X86_REG_ESI,UC_X86_REG_EDI,
    UC_X86_REG_EIP,UC_X86_REG_FPCW,UC_X86_REG_FPSW,UC_X86_REG_FPTAG)

FUNCTIONS=(0x4203a0,0x41f4a0,0x41e6c0,0x573a20,0x419be0,0x401ec0,
           0x401570,0x41abc0,0x420a70,0x420a90,0x420b50,0x41f360,0x41eac0,0x41ac30)
SIZE=32<<20
POLYGONS,QUERY,SP,STOP=0x100000,0x130000,0x2fff00,0x18000


class CPU(C.Structure):
    _fields_=[(s,C.c_uint32) for s in ('eax','ecx','edx','ebx','esp','ebp','esi','edi',
                'df','eflags_ext','fs_base')]+[('st',C.c_double*8)]+[
                (s,C.c_uint32) for s in ('ftop','fsw','fcw')]


def references():
    found={}
    for path in sorted((ROOT/'port/gen').glob('x86_f*.c')):
        text=path.read_text(encoding='utf-8',errors='replace')
        for address in set(FUNCTIONS)-found.keys():
            match=(re.search(r'^void F_%08x_orig\(CPU \*c\) \{'%address,text,re.M) or
                   re.search(r'^void F_%08x\(CPU \*c\) \{'%address,text,re.M))
            if match:
                end=text.find('\nvoid F_',match.end())
                found[address]=text[match.start():end if end>=0 else len(text)].replace(
                    'void F_%08x_orig('%address,'void F_%08x('%address,1)
    assert len(found)==len(FUNCTIONS),'Regenerate port/gen first'
    head='#include "rt.h"\n#include "collision_segment.h"\n#include <stdlib.h>\nuint8_t *g_mem;\n'
    head+='uint32_t g_mem_size=32u<<20;CPU *g_main_cpu;unsigned g_frame,test_failed;\n'
    head+='\n'.join('void F_%08x(CPU *);'%a for a in FUNCTIONS)
    # The reverse-winding diagnostic branch is not used in this initial corpus.
    head+='\nvoid F_00577016(CPU *c) { abort(); }\nvoid rt_call(CPU *c,uint32_t fn) { switch(fn) {\n'
    for a in (0x401ec0,0x401570,0x41abc0,0x420a70,0x420a90,0x420b50):
        head+='case 0x%x: F_%08x(c);break;\n'%(a,a)
    head+='default:abort();}}\n'
    head+='gfn rt_lookup(uint32_t fn) {switch(fn) {\n'
    for a in FUNCTIONS:head+='case 0x%x:return F_%08x;\n'%(a,a)
    head+='default:return NULL;}}\n'
    runtime=(ROOT/'port/src/rt/x86rt.c').read_text()
    first=runtime.index('uint32_t rt_guest_call(');last=runtime.index('\n// ---------- memory',first)
    head+=runtime[first:last]
    head+='''
void F_004203a0_orig(CPU *c) { F_004203a0(c); }
void nat_collision_segments(CPU *c);
void test_bridge(CPU *c) {g_main_cpu=c;nat_collision_segments(c);}
void rt_log(const char *fmt,...) {
    char msg[1024];va_list ap;va_start(ap,fmt);vsnprintf(msg,sizeof msg,fmt,ap);va_end(ap);
    fputs(msg,stdout);
    if(strstr(msg,"failed=") && !strstr(msg,"failed=0 "))test_failed++;
}
void test_enable(void) {
#ifdef _WIN32
    _putenv("SH4_NATIVE_SEGMENTS=1");_putenv("SH4_SEGMENT_VERIFY=1000");
#else
    setenv("SH4_NATIVE_SEGMENTS","1",1);setenv("SH4_SEGMENT_VERIFY","1000",1);
#endif
}
'''
    head+='''
unsigned last_hit_count,last_hits[256];
int test_native(CPU *c) {
    uint8_t *const mb=g_mem;uint32_t sp=c->esp,query=R32(sp+12);
    Sh4SegmentView v={.data=mb,.size=32u<<20,.polygons=R32(sp+4),.count=R32(sp+8),.query_address=query,
        .exclude_begin=sp-0x10000,.exclude_end=sp+16,
        .nearest=R32(0xed7500)==0x420a90 ? 1 : R32(0xed7500)==0x420b50 ? 2 : 0};
    Sh4SegmentQuery q;memcpy(q.ray,mb+query,32);
    if(!sh4_segment_polygons(&v,&q)) return 0;
    last_hit_count=q.count;memcpy(last_hits,q.polygon,q.count*4);
    memcpy(mb+query,q.ray,32);
    for(unsigned i=0;i<q.count;i++) {
        CPU saved=*c;c->esp-=16;W32(c->esp,0x18000);W32(c->esp+4,q.polygon[i]);
        rt_call(c,R32(0xed7500));*c=saved;
    }
    c->eax=0;c->esp+=4;return 1;
}
'''
    return head+'\n'.join(found[a] for a in FUNCTIONS)


def scene(rnd,case):
    count=[0,1,2,7,16,260][case%6]
    data=bytearray()
    for i in range(count):
        z=(i%16)*.125
        n=3+(case+i)%4 if case%9==6 else 4 if (case+i)%3==0 else 3
        vertices=[(-4.,-4.,z),(4.,-4.,z),(4.,4.,z),(-4.,4.,z)] if n==4 else [
                  (-4.,-4.,z),(4.,-4.,z),(-4.,4.,z)]
        if n>4:vertices=[(4*math.cos(j*2*math.pi/n),4*math.sin(j*2*math.pi/n),z) for j in range(n)]
        if case%7==2:
            vertices=[(x,y,z+.11*x-.07*y) for x,y,z in vertices]
        elif case%7==3:
            vertices=[tuple(rnd.uniform(-4,4) for _ in range(3)) for _ in vertices]
        elif case%7==4 and i%2:
            vertices=[vertices[0]]*n
        start=len(data)
        for xyz in vertices:data+=struct.pack('<3fI',*xyz,0x80000000)
        data[start+12]=n
        data[start+13]=1 if n>3 and (case+i)%2 else 0
    if case%5==0: a,b=(.25,.375,-2.),(.25,.375,3.)
    elif case%5==1: a,b=(.25,.375,3.),(.25,.375,-2.)
    elif case%5==2: a,b=(0.,0.,0.),(1.,0.,0.) # parallel/coplanar
    elif case%5==3: a,b=(10.,10.,-2.),(10.,10.,2.) # outside
    else:a,b=[tuple(rnd.uniform(-5,5) for _ in range(3)) for _ in range(2)]
    if case%17==0:a,b=(0.,0.,0.),(0.,0.,3.)
    elif case%17==1:a,b=(0.,0.,-2.),(0.,0.,0.)
    elif case%17==2:a=b=(.25,.375,-2.)
    elif case%17==3:a,b=(0.,0.,-1e-7),(0.,0.,1e-7)
    elif case%17==4:a,b=(-4.,-4.,-2.),(-4.,-4.,3.)
    ray=bytearray(struct.pack('<8f',*a,-2.,*b,3.))
    if case%31==0:struct.pack_into('<I',ray,12,0x7fc12345)
    return bytes(data),count,bytes(ray)


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--cases',type=int,default=2000);ap.add_argument('--cc',default='gcc')
    args=ap.parse_args();game=ROOT/'game/sh4.exe'
    assert hashlib.sha256(game.read_bytes()).hexdigest()==\
        'cf7081a6c78dbe51f6dc2e1c9bd978d8486f1d7b59d7a3b88e027c9b39f21dd0'
    pe=PE(game);rnd=random.Random(0x4203a0)
    with tempfile.TemporaryDirectory(prefix='sh4-segments-',ignore_cleanup_errors=True) as tmp:
        path=Path(tmp)/'refs.c';path.write_text(references(),encoding='utf-8')
        dll=Path(tmp)/('refs.dll' if os.name=='nt' else 'refs.so')
        subprocess.run([args.cc,'-std=gnu11','-O2','-ffp-contract=off','-shared','-fPIC',
                        '-I'+str(ROOT/'port/src/rt'),str(path),str(ROOT/'port/src/rt/collision_segment.c'),
                        str(ROOT/'port/src/rt/collision_segment_bridge.c'),
                        '-o',str(dll),'-lm'],check=True)
        lib=C.CDLL(str(dll));lib.test_enable();memory=C.create_string_buffer(SIZE)
        C.c_void_p.in_dll(lib,'g_mem').value=C.addressof(memory)
        lib.F_004203a0.argtypes=[C.POINTER(CPU)]
        lib.test_native.argtypes=[C.POINTER(CPU)]
        uc=Uc(UC_ARCH_X86,UC_MODE_32);uc.mem_map(0,SIZE)
        original_hits=[];distance_calls=[]
        def capture_callback(uc,address,size,data):
            sp=uc.reg_read(UC_X86_REG_ESP)
            original_hits.append(struct.unpack('<I',uc.mem_read(sp+4,4))[0])
        for cb in (0x420a70,0x420a90,0x420b50):uc.hook_add(UC_HOOK_CODE,capture_callback,begin=cb,end=cb)
        def distance_return(uc,address,size,data):
            sp=uc.reg_read(UC_X86_REG_ESP)
            ray,plane,dst=struct.unpack('<3I',uc.mem_read(sp,12))
            distance_calls.append(dict(result=uc.reg_read(UC_X86_REG_EAX),
                args=(hex(ray),hex(plane),hex(dst)),
                value=bytes(uc.mem_read(dst,4)).hex() if dst<=SIZE-4 else None,
                plane=struct.unpack('<4f',uc.mem_read(plane,16)) if plane<=SIZE-16 else None,
                ray=struct.unpack('<8f',uc.mem_read(ray,32)) if ray<=SIZE-32 else None))
        for ret in (0x420aeb,0x420bab):uc.hook_add(UC_HOOK_CODE,distance_return,begin=ret,end=ret)

        def write(address,data):
            uc.mem_write(address,data);C.memmove(C.addressof(memory)+address,data,len(data))

        for _,va,_,raw,n,_ in pe.sections:
            if n:write(va,pe.data[raw:raw+n])
        for addr,value in [(0x13dc3dc,0x401ec0),(0x13dc3e8,0x401570),(0x13dc3ec,0x41abc0)]:
            write(addr,struct.pack('<I',value))
        regs=[UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_EBX,
              UC_X86_REG_ESP,UC_X86_REG_EBP,UC_X86_REG_ESI,UC_X86_REG_EDI]
        accepted=mutated=saturated=native=refused=0;callbacks={a:0 for a in (0x420a70,0x420a90,0x420b50)}
        for case in range(args.cases):
            data,count,ray=scene(rnd,case);callback=list(callbacks)[case%3]
            global_data=bytearray(rnd.randbytes(0x600))
            struct.pack_into('<I',global_data,0x100,callback)
            struct.pack_into('<I',global_data,0x538,[0,1,255,256][case%4] if case%23==0 else 0)
            struct.pack_into('<I',global_data,0x53c,QUERY)
            write(POLYGONS,data);write(QUERY,ray);write(0xed7400,bytes(global_data))
            stop={0x420a70:0x420c63,0x420a90:0x420c9a,0x420b50:0x420cda}[callback]
            args_data=struct.pack('<4I',stop,POLYGONS,count,QUERY)
            write(SP-0x10000,bytes(0x10100));write(SP,args_data)
            initial=CPU(eax=0x1234,ebx=0x12345678,esi=0x23456789,edi=0x3456789a,
                        ebp=0x456789ab,esp=SP,fcw=[0x23f,0x27f,0x33f,0x37f][case%4],ftop=case%8)
            for reg,(name,_) in zip(regs,CPU._fields_[:8]):uc.reg_write(reg,getattr(initial,name))
            uc.reg_write(UC_X86_REG_FPCW,initial.fcw);uc.reg_write(UC_X86_REG_FPSW,initial.ftop<<11)
            uc.reg_write(UC_X86_REG_FPTAG,0xffff);original_hits.clear();distance_calls.clear();uc.emu_start(0x4203a0,stop,count=3000000)
            assert uc.reg_read(UC_X86_REG_EIP)==stop,(case,'x86 instruction limit')
            cpu=CPU.from_buffer_copy(bytes(initial));lib.F_004203a0(C.byref(cpu))
            generated=CPU.from_buffer_copy(bytes(cpu))
            regions=[(QUERY,32),(0xed7400,0x600),(POLYGONS,len(data))]
            for label in ['generated','native','bridge']:
                if label!='generated':
                    C.memmove(C.addressof(memory)+QUERY,ray,32)
                    C.memmove(C.addressof(memory)+0xed7400,bytes(global_data),0x600)
                    C.memset(C.addressof(memory)+SP-0x10000,0,0x10100)
                    C.memmove(C.addressof(memory)+SP,args_data,16)
                    cpu=CPU.from_buffer_copy(bytes(initial))
                    if label=='bridge':lib.test_bridge(C.byref(cpu))
                    elif not lib.test_native(C.byref(cpu)):
                        assert bytes(cpu)==bytes(initial),(case,'CPU changed on refusal')
                        assert C.string_at(C.addressof(memory)+QUERY,32)==ray
                        assert C.string_at(C.addressof(memory)+0xed7400,0x600)==bytes(global_data)
                        refused+=1;cpu=generated;continue
                    else:
                        native+=1
                        assert all(x['result'] for x in distance_calls),(case,'accepted an uninitialized callback distance')
                        nh=C.c_uint.in_dll(lib,'last_hit_count').value
                        hit_list=list((C.c_uint*256).in_dll(lib,'last_hits'))[:nh]
                        assert original_hits==hit_list,(case,'hit sequence',original_hits,hit_list)
                for address,n in regions:
                    a=bytes(uc.mem_read(address,n));b=C.string_at(C.addressof(memory)+address,n)
                    if a!=b:
                        off=next(i for i in range(n) if a[i]!=b[i]);raise AssertionError((case,label,hex(callback),hex(address+off),a[off:off+8].hex(),b[off:off+8].hex(),distance_calls))
                assert not C.c_uint.in_dll(lib,'test_failed').value,(case,'production verification failed')
            for reg,name in [(UC_X86_REG_EAX,'eax'),(UC_X86_REG_ESP,'esp'),(UC_X86_REG_EBX,'ebx'),
                             (UC_X86_REG_ESI,'esi'),(UC_X86_REG_EDI,'edi'),(UC_X86_REG_EBP,'ebp')]:
                assert uc.reg_read(reg)==getattr(cpu,name),(case,name)
            assert cpu.ftop==((uc.reg_read(UC_X86_REG_FPSW)>>11)&7)==initial.ftop
            assert bytes(uc.mem_read(POLYGONS,len(data)))==data,(case,'source polygon writes')
            result=struct.unpack('<I',uc.mem_read(0xed7938,4))[0]
            accepted+=result!=0;mutated+=bytes(uc.mem_read(QUERY,32))!=ray;saturated+=result==256
            callbacks[callback]+=1
        assert accepted and mutated and saturated,(accepted,mutated,saturated)
        print(f'{args.cases} original-x86/generated segment-list queries agree: {accepted} nonempty lists, '
              f'{mutated} mutable queries, {saturated} saturated collectors; all three callbacks exercised {callbacks}; '
              f'native accepted {native}, transactional refusals {refused}')
        faults=['range','count','query-alias','global-alias','stack-alias','nan','extreme','hit-cap','undefined-distance']
        for fault in faults:
            data=bytearray()
            for _ in range(260 if fault=='hit-cap' else 1):
                for xyz in [(-4.,-4.,0.),(4.,-4.,0.),(4.,4.,0.),(-4.,4.,0.)]:data+=struct.pack('<3fI',*xyz,0)
                data[-64+12]=4
            ray=struct.pack('<8f',.25,.375,-2.,-2.,.25,.375,3.,3.)
            poly=POLYGONS;count=260 if fault=='hit-cap' else 1;cb=0x420a70
            if fault=='range':poly=SIZE-8
            elif fault=='count':count=0xffffffff
            elif fault=='query-alias':poly=QUERY
            elif fault=='global-alias':poly=0xed7400
            elif fault=='stack-alias':poly=SP
            elif fault=='nan':ray=struct.pack('<I7f',0x7fc00001,0.,-2.,0.,0.,0.,2.,0.)
            elif fault=='extreme':ray=struct.pack('<8f',1e20,0.,-2.,0.,0.,0.,2.,0.)
            elif fault=='undefined-distance':
                data=bytearray().join(struct.pack('<3fI',*v,0) for v in [(-1.,-1.,0.),(1.,-1.,0.),(1.,1.,0.),(-1.,1.,1.)])
                data[12]=4;ray=struct.pack('<8f',-.5,.5,.5,0.,-.5,.5,1.,0.);cb=0x420a90
            C.memmove(C.addressof(memory)+POLYGONS,bytes(data),len(data))
            globals_=bytearray(0x600);struct.pack_into('<I',globals_,0x100,cb);struct.pack_into('<I',globals_,0x53c,QUERY)
            C.memmove(C.addressof(memory)+0xed7400,bytes(globals_),len(globals_));C.memmove(C.addressof(memory)+QUERY,ray,32)
            C.memmove(C.addressof(memory)+SP,struct.pack('<4I',STOP,poly,count,QUERY),16)
            cpu=CPU(esp=SP,fcw=0x37f);saved=bytes(cpu)
            assert lib.test_native(C.byref(cpu))==0,(fault,'accepted')
            assert bytes(cpu)==saved and C.string_at(C.addressof(memory)+QUERY,32)==ray,(fault,'partial state')
            assert C.string_at(C.addressof(memory)+0xed7400,0x600)==bytes(globals_),(fault,'callback executed on refusal')
        print(f'{len(faults)} invalid/unsupported views refuse transactionally, including nearest callback uninitialized-distance case')
        # The empty-list ABI must not dereference unused geometry/ray pointers.
        for count in (0,0xffffffff):
            globals_=bytearray(0x600);struct.pack_into('<I',globals_,0x100,0x420a70)
            C.memmove(C.addressof(memory)+0xed7400,bytes(globals_),len(globals_))
            C.memmove(C.addressof(memory)+SP,struct.pack('<4I',0x420c63,0xffffffff,count,0xffffffff),16)
            initial=CPU(esp=SP,fcw=0x23f,ebx=123,esi=456,edi=789,ebp=321)
            reference=CPU.from_buffer_copy(bytes(initial));lib.F_004203a0(C.byref(reference))
            actual=CPU.from_buffer_copy(bytes(initial));lib.test_bridge(C.byref(actual))
            for field in ('eax','esp','ebx','esi','edi','ebp','ftop','fcw'):
                assert getattr(reference,field)==getattr(actual,field),('empty ABI',count,field)
            assert C.string_at(C.addressof(memory)+0xed7400,0x600)==bytes(globals_)
        print('Empty and negative-count production entries preserve the original ABI without dereferencing unused invalid pointers')
        if os.name=='nt':
            import _ctypes
            _ctypes.FreeLibrary(lib._handle)


if __name__=='__main__':main()
