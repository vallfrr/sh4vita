"""Exercise the production buffer lock/cache bridge against uncached bounds.

Compile the actual bridge and Lock/Unlock bodies with a minimal COM registry;
the actual native bounds pass is linked unchanged. The original-x86 bounds
oracle is covered separately by model_uv_original_test.py.
"""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[2]


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--cc',default='gcc');args=ap.parse_args()
    resource=(ROOT/'port/src/d3d8/resource.c').read_text()
    begin=resource.index('uint32_t g_buf_ver;')
    end=resource.index('M(buf, GetDesc)',begin)
    bridge=resource[begin:end]
    header=(ROOT/'port/src/d3d8/d3d8.h').read_text()
    structs='\n'.join(re.search(r'typedef struct '+name+r' \{.*?\} '+name+';',header,re.S)[0] for name in ['ComObj','Buffer'])
    source=r'''
#include "rt.h"
#include "model_uv.h"
#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
typedef unsigned GLuint;
enum {K_VB=4,K_IB=5};
#define HR_OK 0u
#define M(p,n) static void p##_##n(CPU *c)
uint8_t *g_mem;uint32_t g_mem_size=32u<<20;CPU *g_main_cpu;
'''+structs+r'''
static Buffer buffer;
ComObj *com_get(uint32_t guest) { return guest==buffer.com.guest ? &buffer.com : NULL; }
void *com_get_kind(uint32_t guest,int kind) { ComObj *o=com_get(guest);return o && o->kind==kind ? o : NULL; }
void prep_wait_use(uint32_t use) { (void)use; }
void rt_log(const char *fmt,...) { (void)fmt; }
'''+bridge+r'''
void F_00415ef0_orig(CPU *c) { uint32_t sp=c->esp;buf_Lock(c);c->esp=sp+4; }
void F_00415ee0_orig(CPU *c) { uint32_t sp=c->esp;buf_Unlock(c);c->esp=sp+4; }
static unsigned random_state=0x4290c0;
static unsigned word(void) { random_state=random_state*1664525u+1013904223u;return random_state; }
static void lock_buffer(CPU *c,int readonly) {
    uint8_t *mb=g_mem;c->esp=0x31000;
    W32(c->esp,readonly ? 0x4290ac : 0x428fb6);
    W32(c->esp+4,buffer.com.guest);W32(c->esp+8,0);W32(c->esp+12,0);
    W32(c->esp+16,0x32000);W32(c->esp+20,0);
    nat_model_buffer_lock(c);assert(R32(0x32000)==buffer.mem);
}
static void unlock_buffer(CPU *c,int readonly) {
    uint8_t *mb=g_mem;c->esp=0x31000;
    W32(c->esp,readonly ? 0x42928f : 0x42903d);W32(c->esp+4,buffer.com.guest);
    nat_model_buffer_unlock(c);
}
static void compare(CPU *c,unsigned count,unsigned seed) {
    uint8_t *mb=g_mem;const unsigned sp=0x30000;
    for(unsigned i=0;i<128;i++) W8(sp-16+i,(seed+i)&255);
    W32(sp+0x34,0);W32(sp+0x1c,0);W32(sp+0x30,0x40000000);W32(sp+0x28,0x40000000);
    if(seed%17==0) W32(sp+0x30,0x80000000);
    c->esp=sp;c->ecx=buffer.mem;c->edx=count;c->edi=0x12345678;
    CPU reference=*c,initial=*c;uint8_t stack[128],expected[128];
    memcpy(stack,mb+sp-16,128);assert(nat_model_bounds(&reference));memcpy(expected,mb+sp-16,128);
    memcpy(mb+sp-16,stack,128);assert(d3d_model_bounds(c));
    assert(!memcmp(c,&reference,sizeof *c));assert(!memcmp(mb+sp-16,expected,128));
    assert(initial.esi==c->esi && initial.ebx==c->ebx);
}
static void compare_uv(CPU *c,unsigned count,unsigned seed) {
    uint8_t *mb=g_mem;const unsigned sp=0x30000,dst=0xf529b0;
    c->esp=sp;c->ecx=buffer.mem;c->edi=dst+28;c->esi=0x10000;
    W32(c->esi+0x44,count);
    W32(sp+0x30,0);W32(sp+0x28,rt_u32f(0.125f));W32(sp+0x1c,rt_u32f(seed%2?0.25f:0.375f));
    W32(sp+0x24,rt_u32f(seed%3?0.245f:0.5f));W32(sp+0x10,rt_u32f(seed%5?0.25f:0.5f));
    W32(sp+0x40,rt_u32f((float)(seed%7)*0.125f));W32(sp+0x44,rt_u32f((float)(seed%11)*0.0625f));
    uint8_t stack[128],expected_stack[128],expected_vertices[64*64];
    memcpy(stack,mb+sp-16,128);CPU reference=*c;
    assert(nat_model_uv(&reference));memcpy(expected_stack,mb+sp-16,128);
    memcpy(expected_vertices,mb+dst,count*64);
    memcpy(mb+sp-16,stack,128);memset(mb+dst,0xa5,count*64);
    assert(d3d_model_uv(c));assert(!memcmp(c,&reference,sizeof *c));
    assert(!memcmp(mb+sp-16,expected_stack,128));assert(!memcmp(mb+dst,expected_vertices,count*64));
}
int main(void) {
#ifdef _WIN32
    _putenv("SH4_NATIVE_VERTICES=1");_putenv("SH4_NATIVE_MODEL_UV_CACHE=2");_putenv("SH4_MODEL_UV_CACHE_VERIFY=10000");
#else
    setenv("SH4_NATIVE_VERTICES","1",1);setenv("SH4_NATIVE_MODEL_UV_CACHE","2",1);setenv("SH4_MODEL_UV_CACHE_VERIFY","10000",1);
#endif
    g_mem=calloc(1,g_mem_size);assert(g_mem);CPU cpu={0};g_main_cpu=&cpu;
    buffer.com=(ComObj){.guest=0x1400100,.refs=1,.kind=K_VB};buffer.mem=0x1401000;buffer.size=64*64;
    uint8_t *mb=g_mem;
    for(unsigned i=0;i<64*64/4;i++) W32(buffer.mem+i*4,word());
    for(unsigned i=0;i<64;i++) {
        W32(buffer.mem+i*64+24,rt_u32f(((int)(word()%1024)-512)/128.f));
        W32(buffer.mem+i*64+28,rt_u32f(((int)(word()%1024)-512)/128.f));
    }
    W64(0x5b7b08,rt_u64f(0.25));W32(0x5b7b10,rt_u32f(0.25f));
    W32(0x5b7b14,rt_u32f(0.26f));W32(0x5b7b18,rt_u32f(0.24f));
    for(unsigned test=0;test<50000;test++) {
        if(test%101==0) {
            lock_buffer(&cpu,0);W32(buffer.mem+24,word());W32(buffer.mem+28,word());
            unlock_buffer(&cpu,0);assert(!buffer.model_bounds_n);
        }
        if(test%997==0) { // destruction and same guest/payload address reused
            buffer.model_bounds_n=buffer.model_read_ver=0;buffer.model_reader=NULL;
            W32(buffer.mem+64+24,word());
        }
        lock_buffer(&cpu,1);
        compare(&cpu,32,test);
        unsigned hits=model_bounds_hits;
        compare(&cpu,32,test);assert(model_bounds_hits==hits+1);
        compare(&cpu,17,test); // vertex count belongs to cache identity
        compare_uv(&cpu,17,test);compare_uv(&cpu,17,test);
        unlock_buffer(&cpu,1);assert(!buffer.model_reader && !buffer.model_read_ver);
    }
    assert(model_bounds_verified==10000 && model_uv_verified==10000 && model_bounds_cache_mode==2);
    assert(model_bounds_hits>=50000 && model_bounds_misses>50000);
    // A failed live verification disables reuse and keeps the recomputed data.
    lock_buffer(&cpu,1);compare(&cpu,32,1);
    buffer.model_extrema[0]^=1;model_bounds_verify=model_bounds_verified+1;
    compare(&cpu,32,1);assert(!model_bounds_cache_mode && !buffer.model_bounds_n);
    model_uv_release(&buffer);assert(!model_uv_bytes && !buffer.model_uv);
    printf("250002 cached/uncached bounds+UV comparisons passed: full CPU/stack/vertices, generic write invalidation, read scope, parameter/count/initial changes, address reuse, live mismatch fallback, cache budget release\n");
    free(g_mem);return 0;
}
'''
    with tempfile.TemporaryDirectory(prefix='sh4-model-ownership-') as tmp:
        path=Path(tmp)/'probe.c';path.write_text(source)
        exe=Path(tmp)/'probe.exe'
        subprocess.run([args.cc,'-std=gnu11','-O2','-ffp-contract=off','-I'+str(ROOT/'port/src/rt'),
                        str(path),str(ROOT/'port/src/rt/model.c'),str(ROOT/'port/src/rt/model_uv.c'),'-o',str(exe),'-lm'],check=True)
        subprocess.run([str(exe)],check=True)


if __name__=='__main__':main()
