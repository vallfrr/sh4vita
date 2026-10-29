"""Production DirectSound child interfaces must retain their parent buffer."""
from pathlib import Path
import subprocess,tempfile,os
root=Path(__file__).resolve().parents[2]
s=(root/'port/src/dx/dsound.c').read_text()
def section(a,b):return s[s.index(a):s.index(b,s.index(a))]
sub=section('M(sub, Release) {','#define B3')
query=section('M(buf, QueryInterface) {','M(buf, AddRef)')
release=section('static uint32_t buf_release_ref(DSBuf *b) {','M(buf, GetCaps)')
head=r'''
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
typedef struct { uint32_t guest;int refs,kind; } ComObj;
typedef struct DSBuf {ComObj com;struct DSBuf *next;int *shared_refs;uint32_t size,mem;} DSBuf;
typedef struct {ComObj com;DSBuf *b;} DSSub;
typedef struct {uint32_t args[4],result;} CPU;
enum {K_DSBUF=1,K_DS3DB,K_DSNOTIFY,K_DS3DL};
static ComObj *objects[64],*listener;static DSBuf *bufs;static void *mx;
static unsigned next_id=1,destroyed_parent,guest_frees;uint64_t g_mem_ds;
static uint8_t memory[4096];static int l3_vt[18],b3_vt[21],nt_vt[4];
#define HOSTFN
#define ARG(n) c->args[n]
#define R32(p) (*(uint32_t *)(memory+(p)))
#define W32(p,v) (R32(p)=(v))
#define M(a,b) void a##_##b(CPU *c)
#define STDRET(n,v) do {c->result=(v);return;}while(0)
#define HR_OK 0
#define HR_FAIL 0x80004005u
#define BUF DSBuf *b=com_get_kind(ARG(1),K_DSBUF);if(!b)STDRET(1,HR_FAIL)
static void SDL_LockMutex(void *p){(void)p;}static void SDL_UnlockMutex(void *p){(void)p;}
static ComObj *com_get(uint32_t id){return id<64?objects[id]:NULL;}
static void *com_get_kind(uint32_t id,int kind){ComObj *o=com_get(id);return o&&o->kind==kind?o:NULL;}
static void com_wrap(void *ptr,int kind,void *vt,int n,const char *name){(void)vt;(void)n;(void)name;ComObj *o=ptr;o->guest=next_id++;o->kind=kind;o->refs=1;objects[o->guest]=o;}
static void com_free(ComObj *o){if(o->kind==K_DSBUF)destroyed_parent++;objects[o->guest]=NULL;}
static void gheap_free(uint32_t p){assert(p==100);guest_frees++;}
static void rt_log(const char *fmt,...){(void)fmt;}
static uint32_t buf_release_ref(DSBuf *b);
'''
test=r'''
static uint32_t parent(void){DSBuf *b=calloc(1,sizeof *b);b->shared_refs=malloc(sizeof(int));*b->shared_refs=1;b->size=64;b->mem=100;g_mem_ds+=64;com_wrap(b,K_DSBUF,NULL,0,"parent");bufs=b;return b->com.guest;}
static uint32_t child(uint32_t p,uint32_t iid){CPU c={0};c.args[1]=p;c.args[2]=64;c.args[3]=128;W32(64,iid);buf_QueryInterface(&c);assert(c.result==0);return R32(128);}
static void drop(uint32_t id,int sub){CPU c={0};c.args[1]=id;if(sub)sub_Release(&c);else buf_Release(&c);}
int main(int argc,char **argv){
 (void)argc;(void)argv;
 for(unsigned scenario=0;scenario<3;scenario++){
  unsigned before=destroyed_parent;uint32_t p=parent();
  uint32_t a=child(p,0x279afa86u),b=child(p,0xb0210783u);
  assert(com_get(p)->refs==3);
  if(scenario==0){drop(p,0);assert(com_get(p));drop(a,1);assert(com_get(p));drop(b,1);}
  if(scenario==1){drop(a,1);drop(b,1);assert(com_get(p)->refs==1);drop(p,0);}
  if(scenario==2){com_get(a)->refs++;drop(p,0);drop(a,1);assert(com_get(p));drop(b,1);assert(com_get(p));drop(a,1);}
  assert(!com_get(p)&&destroyed_parent==before+1&&!bufs&&g_mem_ds==0);
 }
 assert(guest_frees==3);
 puts("3D/notification interfaces retain the parent across release orders; storage freed exactly once");
}
'''
with tempfile.TemporaryDirectory(prefix='sh4-audio-life-') as d:
    c=Path(d)/'test.c';exe=Path(d)/'test.exe';c.write_text(head+release+query+sub+test)
    subprocess.run(['gcc','-std=c11','-O2',str(c),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
    # The old implementation fails before the caller can safely retain children.
    c.write_text((head+release+query+sub+test).replace('b->com.refs++; // The child interface owns its parent until its final Release.',''))
    subprocess.run(['gcc','-std=c11','-O2',str(c),'-o',str(exe)],check=True)
    result=subprocess.run([str(exe)],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    assert result.returncode!=0,'missing parent ownership was not detected'
    print('Negative control without the ownership fix is rejected')
