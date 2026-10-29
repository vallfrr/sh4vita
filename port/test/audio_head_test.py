from pathlib import Path
import re,subprocess,tempfile,os
r=Path(__file__).resolve().parents[2];s=(r/'port/src/win32/kernel32.c').read_text();file=re.search(r'typedef struct \{\n    FILE \*f; char path\[600\].*?\n\} File;',s,re.S).group()
head=r'''
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#define PF_MIN 262144u
#define PF_MAX 1310720u
typedef struct{int value;} SDL_atomic_t;typedef int SDL_Thread;
typedef struct{uint32_t args[8],result,error,esp;} CPU;
'''+file+r'''
static uint32_t s_pf_mem;
static int fail_alloc,io_calls,kind=1;static File *opened;
static uint8_t memory[8<<20];static const uint32_t g_mem_size=sizeof memory;
static CPU main_cpu,worker_cpu,*g_main_cpu=&main_cpu;
static uint64_t g_t_io,g_io_bytes;static unsigned g_frame;
#define K(name) static void imp_##name(CPU *c)
#define HOSTFN uint8_t *mb=memory;(void)mb
#define ARG(n) c->args[n]
#define R32(p) (*(uint32_t*)(memory+(p)))
#define W32(p,v) (R32(p)=(v))
#define STDRET(n,v) do{c->result=(v);return;}while(0)
#define H_FREE 0
static File *file_of(uint32_t h){return h==4&&kind?opened:NULL;}
static int h_type(uint32_t h){return h==4?kind:0;}
static void h_close(uint32_t h){assert(h==4);kind=0;opened=NULL;}
static void pf_stop(File *f){f->th=NULL;}
static int pf_take(File *f,long p,uint8_t *dst,uint32_t n){(void)f;(void)p;(void)dst;(void)n;return 0;}
static void pf_plan(File *f,long p,uint32_t n){(void)f;(void)p;(void)n;}
static void w32_set_last_error(CPU *c,uint32_t e){c->error=e;}
static void ev_set(uint32_t h,int v){(void)h;(void)v;}
static void rt_log(const char *fmt,...){(void)fmt;errno=EINVAL;}
static uint64_t SDL_GetPerformanceCounter(void){static uint64_t n;return n+=1000;}
static uint64_t SDL_GetPerformanceFrequency(void){return 1000000;}
#define SDL_strcasecmp _stricmp
#define SDL_memset memset
static void *head_alloc(void *p,size_t n){return fail_alloc?NULL:realloc(p,n);}
#define realloc head_alloc
'''
cache=s[s.index('static int pf_reserve('):s.index('static void pf_start(')]
read=s[s.index('static uint32_t read_direct('):s.index('// Translate storage failures')]
errors=s[s.index('static uint32_t file_error(int error, uint32_t fallback) {'):s.index('K(WriteFile)')]
close=s[s.index('K(CloseHandle)'):s.index('K(CreateDirectoryA)')]
tail=r'''
static uint8_t value_at(unsigned p){return (uint8_t)((p*131u)^(p>>7)^(p>>16));}
static const unsigned total=4u<<20,buf=0x4000,out=0x100;
static size_t read_count(void *p,size_t z,size_t n,FILE *f){++io_calls;return fread(p,z,n,f);}
#define fread read_count
'''
# Count the actual I/O calls inside the production functions, not the oracle.
read='#define fread read_count\n'+read+'\n#undef fread\n'
head+='\nstatic size_t read_count(void*,size_t,size_t,FILE*);\nstatic uint32_t file_error(int,uint32_t);\nstatic uint32_t file_status(uint32_t);\n'
main=r'''
static void request(CPU *c,unsigned pos,unsigned n,int expect_cached){
 assert(opened&&pos<=total);assert(!fseek(opened->f,pos,SEEK_SET));memset(memory+buf-16,0xa5,n+32);
 memset(c,0,sizeof *c);c->args[1]=4;c->args[2]=buf;c->args[3]=n;c->args[4]=out;c->error=777;
 int before=io_calls;imp_ReadFile(c);assert(c->result&&c->error==777);
 unsigned got=n<total-pos?n:total-pos;assert(R32(out)==got&&ftell(opened->f)==(long)(pos+got));
 for(unsigned i=0;i<n;i++)assert(memory[buf+i]==(i<got?value_at(pos+i):0));
 for(unsigned i=0;i<16;i++)assert(memory[buf-16+i]==0xa5&&memory[buf+n+i]==0xa5);
 if(expect_cached)assert(io_calls==before);else if(n)assert(io_calls>before);
 assert(s_pf_mem<=2*PF_MAX);
}
int main(void){
 int enabled=getenv("SH4_AUDIO_HEAD_CACHE")&&!strcmp(getenv("SH4_AUDIO_HEAD_CACHE"),"1");
 opened=calloc(1,sizeof *opened);assert(opened);opened->f=tmpfile();assert(opened->f);opened->ro=opened->pfreuse=1;
 strcpy(opened->path,"ux0:data/sh4/demo/BGM.SH4");uint8_t *data=malloc(total);assert(data);
 for(unsigned i=0;i<total;i++)data[i]=value_at(i);assert(fwrite(data,1,total,opened->f)==total);free(data);
 request(&main_cpu,0,1228800,0);assert((pf_head.owner==opened)==enabled);
 if(enabled)assert(pf_head.capacity==(1u<<20)&&s_pf_mem==(1u<<20));
 unsigned hits=0;
 for(unsigned i=0;i<256;i++){
  unsigned pos=(i*3557u)%(1u<<20),n=(i&1)?614400:PF_MIN;
  int fit=enabled&&(uint64_t)pos+n<=(1u<<20);request(&main_cpu,pos,n,fit);hits+=fit;
 }
 request(&worker_cpu,292864,614400,0); // A foreign CPU neither reads nor mutates the head cache.
 request(&main_cpu,292864,614400,enabled); // The real hospital loop-restart range.
 request(&main_cpu,2u<<20,1228800,0); // Replace the cached prefix with another region in the same archive.
 request(&main_cpu,(2u<<20)+292864,614400,enabled);
 request(&main_cpu,292864,614400,0); // The previous prefix must no longer be served.
 request(&main_cpu,total-100,PF_MIN,0); // Partial EOF cannot populate the cache.
 if(enabled){
  unsigned hold=2*PF_MAX-s_pf_mem-64;assert(pf_reserve(hold));File other={0};pf_head_pressure(&other,PF_MAX);
  assert(!pf_head.owner&&s_pf_mem==hold);pf_release(hold);assert(!s_pf_mem);
  fail_alloc=1;request(&main_cpu,0,1228800,0);assert(!pf_head.owner&&!s_pf_mem);fail_alloc=0;
 }
 pf_head_forget(opened);assert(!s_pf_mem);
 opened->ro=0;request(&main_cpu,0,1228800,0);assert(!pf_head.owner);
 opened->ro=1;opened->pfreuse=0;request(&main_cpu,0,1228800,0);assert(!pf_head.owner);
 opened->pfreuse=1;strcpy(opened->path,"ux0:data/sh4/demo/cm.sh4");request(&main_cpu,0,1228800,0);assert(!pf_head.owner);
 strcpy(opened->path,"ux0:data/sh4/demo/bgm.sh4");request(&main_cpu,0,1228800,0);
 main_cpu.args[1]=4;imp_CloseHandle(&main_cpu);assert(main_cpu.result&&!opened&&!pf_head.owner&&!s_pf_mem);
 printf("256 real ReadFile ranges (%u cache hits), hospital loop, worker exclusion, EOF/padding/positions/guards, pressure/allocation failure, policy and CloseHandle cleanup pass\n",hits);
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(head+cache+'\n#undef realloc\n'+read+errors+close+tail+main)
 exe=p/'test.exe';subprocess.run([str(r.parent/'tools/mingw64/bin/gcc.exe'),'-std=c11','-O2','-Wall','-Wextra','-Wno-unused-function','-Wno-misleading-indentation',str(p/'test.c'),'-o',str(exe)],check=True)
 for flag in ['0','1']:
  print('HEAD',flag,flush=True);subprocess.run([str(exe)],env={**os.environ,'SH4_AUDIO_HEAD_CACHE':flag},check=True)
