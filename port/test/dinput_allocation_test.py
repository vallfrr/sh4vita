"""Inject host/guest allocation failures into the production input entry points."""
from pathlib import Path
import os, shutil, subprocess, tempfile
root=Path(__file__).resolve().parents[2]
src=(root/'port/src/dx/dinput.c').read_text(encoding='utf-8')
def extract(mark):
    a=src.index(mark);return src[a:src.index('\n}',a)+2]
types=src[src.index('typedef struct { ComObj com; } DInput;'):src.index('#define M(')]
guids=src[src.index('#define M('):src.index('// ---------- clavier')]
bodies='\n'.join(extract(x) for x in ('M(dev, EnumObjects) {','M(di, CreateDevice) {','M(di, EnumDevices) {','void imp_DINPUT8_DirectInput8Create(CPU *c) {'))
head=r'''
#define SDL_MAIN_HANDLED
#include "d3d8/d3d8.h"
#include "dx/pad_sample.h"
#include <assert.h>
#include <stdlib.h>
uint8_t *g_mem;uint32_t g_mem_size=65536;
static int host_fail,guest_fail,callbacks,releases,wraps;
static void *allocate(size_t count,size_t bytes) { return host_fail?NULL:calloc(count,bytes); }
#define calloc allocate
uint32_t gheap_calloc(uint32_t n) { if(guest_fail)return 0;memset(g_mem+0x1000,0,n);return 0x1000; }
void gheap_free(uint32_t p) { assert(p==0x1000);++releases; }
void rt_log(const char *fmt,...) {(void)fmt;}
uint32_t rt_guest_call(CPU *c,uint32_t fn,int count,...) { (void)c;assert(fn==0x1234 && count==2);++callbacks;return 1; }
static const gfn dev_vt[32]={0},di_vt[11]={0};
uint32_t com_wrap(ComObj *o,int kind,const gfn *vt,int count,const char *name) {
 assert(o && name && ((kind==K_DIDEV && vt==dev_vt && count==32)||(kind==K_DINPUT && vt==di_vt && count==11)));++wraps;return 0x8000;
}
static void *current;
void *com_get_kind(uint32_t p,int kind) { assert(p==0x9000 && kind==K_DIDEV);return current; }
SDL_GameController *SDL_GameControllerOpen(int index) { (void)index;return (void*)1; }
int SDL_NumJoysticks(void) { return 1; }
SDL_bool SDL_IsGameController(int index) { assert(index==0);return SDL_TRUE; }
const char *SDL_GameControllerNameForIndex(int index) { assert(index==0);return "test"; }
size_t SDL_strlcpy(char *dst,const char *src,size_t n) { size_t length=strlen(src);if(n){size_t k=length<n?length:n-1;memcpy(dst,src,k);dst[k]=0;}return length; }
'''
tail=r'''
static CPU start(void) { CPU c={.esp=0x400};uint8_t *mb=g_mem;W32(0x404,0x9000);W32(0x408,0x1234);W32(0x40c,0x2000);W32(0x410,0x2000);return c; }
int main(void) {
 g_mem=malloc(g_mem_size);assert(g_mem);memset(g_mem,0xa5,g_mem_size);uint8_t *mb=g_mem;
 DIDev device={.type=2};current=&device;CPU c;
 // Failed enumeration must never write a guest structure at address zero.
 guest_fail=1;c=start();dev_EnumObjects(&c);assert(c.eax==0x8007000eu && c.esp==0x414 && !callbacks && !releases);
 c=start();W32(0x408,4);W32(0x40c,0x1234);di_EnumDevices(&c);assert(c.eax==0x8007000eu && c.esp==0x418 && !callbacks && !releases);
 for(int i=0;i<64;i++)assert(mb[i]==0xa5);
 // Host metadata failure returns an invalidated output, not a null dereference.
 host_fail=1;c=start();W32(0x408,0x3000);di_CreateDevice(&c);assert(c.eax==0x8007000eu && c.esp==0x414 && !R32(0x2000) && !wraps);
 W32(0x2000,0xdeadbeef);c=start();imp_DINPUT8_DirectInput8Create(&c);assert(c.eax==0x8007000eu && c.esp==0x418 && !R32(0x2000) && !wraps);
 guest_fail=host_fail=0;c=start();dev_EnumObjects(&c);assert(!c.eax && callbacks==6 && releases==1);
 c=start();W32(0x408,4);W32(0x40c,0x1234);di_EnumDevices(&c);assert(!c.eax && callbacks==7 && releases==2);
 memcpy(mb+0x3000,GUID_KBD,16);c=start();W32(0x408,0x3000);di_CreateDevice(&c);assert(!c.eax && R32(0x2000)==0x8000 && wraps==1);
 c=start();imp_DINPUT8_DirectInput8Create(&c);assert(!c.eax && R32(0x2000)==0x8000 && wraps==2);
 free(g_mem);puts("input allocation: host/guest OOM leaves null outputs and guest address zero intact; successful enumeration/creation ABI retained");
}
'''
with tempfile.TemporaryDirectory(prefix='sh4-input-oom-') as tmp:
    p=Path(tmp);(p/'test.c').write_text(head+types+guids+bodies+tail,encoding='utf-8')
    cc=os.environ.get('CC') or shutil.which('gcc') or 'J:/tools/mingw64/bin/gcc.exe'
    subprocess.run([cc,'-std=gnu11','-O2','-I'+str(root/'port/src'),'-I'+str(root/'port/src/rt'),'-I'+str(root/'port/gen'),
        '-I'+str(root.parent/'tools/sdl2/SDL2-2.32.10/x86_64-w64-mingw32/include'),
        '-I'+str(root.parent/'tools/sdl2/SDL2-2.32.10/x86_64-w64-mingw32/include/SDL2'),
        str(p/'test.c'),'-lm','-o',str(p/'test.exe')],check=True)
    subprocess.run([str(p/'test.exe')],check=True)
