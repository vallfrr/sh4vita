"""Fault injection against the production DirectSound creation function."""
from pathlib import Path
import subprocess, tempfile
root = Path(__file__).resolve().parents[2]
source = (root / 'port/src/dx/dsound.c').read_text()
start = source.index('M(ds, CreateSoundBuffer) {')
function = source[start:source.index('\nM(ds, GetCaps)', start)]
head = r'''
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct { uint32_t args[5], result; } CPU;
static unsigned char memory[4096];
#define HOSTFN
#define ARG(n) (c->args[n])
#define R32(a) (*(uint32_t *)(memory+(a)))
#define R16(a) (*(uint16_t *)(memory+(a)))
#define W32(a,v) (R32(a)=(v))
#define M(a,b) void a##_##b(CPU *c)
#define STDRET(n,v) do { c->result=(v); return; } while(0)
#define HR_OK 0
#define K_DSBUF 1
typedef struct { uint32_t guest; } Com;
typedef struct { Com com; uint32_t flags,primary,is3d,ch,rate,align,bits,size,mem; int *shared_refs; } DSBuf;
static int failure,links,wraps,guest_live;
static DSBuf *last;
static uint64_t g_mem_ds;
static int buf_vt[24];
static void *test_calloc(size_t n,size_t size) { return failure==3?NULL:calloc(n,size); }
#define calloc test_calloc
static DSBuf *buf_new(void) { return failure==1?NULL:(last=malloc(sizeof(DSBuf))); }
static uint32_t gheap_calloc(uint32_t n) { (void)n; if(failure==2)return 0; guest_live++;return 1024; }
static void gheap_free(uint32_t p) { assert(p==1024);guest_live--; }
static void com_wrap(Com *o,int k,void *v,int n,const char *s) {(void)k;(void)v;(void)n;(void)s;wraps++;o->guest=2048;}
static void buf_link(DSBuf *b) {(void)b;links++;}
'''
# The production function declares its counter extern.
head = head.replace('static uint64_t g_mem_ds;', 'uint64_t g_mem_ds;')
test = r'''
int main(void) {
 CPU c={0};c.args[2]=64;c.args[3]=128;
 W32(68,0);W32(72,256);W32(80,160);W32(164,48000);
 for(failure=1;failure<=3;failure++) {
  W32(128,0xdeadbeef);ds_CreateSoundBuffer(&c);
  assert(c.result==0x8007000e && R32(128)==0);
  assert(!guest_live && !links && !wraps && !g_mem_ds);
 }
 failure=0;ds_CreateSoundBuffer(&c);
 assert(c.result==0 && R32(128)==2048 && guest_live==1);
 assert(links==1 && wraps==1 && g_mem_ds==256 && *last->shared_refs==1);
 free(last->shared_refs);free(last);
 puts("DirectSound host/guest allocation failures return cleanly; success path retained");
}
'''
with tempfile.TemporaryDirectory(prefix='sh4-dsound-') as folder:
    code=Path(folder)/'test.c'; exe=Path(folder)/'test.exe'
    code.write_text(head+function+test)
    subprocess.run(['gcc','-std=c11','-O2',str(code),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
