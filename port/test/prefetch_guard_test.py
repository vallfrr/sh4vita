"""Production planner/range-copy checks; optional replay of a real streaming log."""
from pathlib import Path
import argparse,re,os,subprocess,tempfile
r=Path(__file__).resolve().parents[2]
ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--trace',type=Path);args=ap.parse_args()
s=(r/'port/src/win32/kernel32.c').read_text(encoding='utf-8')
file=re.search(r'typedef struct \{\n    FILE \*f; char path\[600\].*?\n\} File;',s,re.S).group()
take=s[s.index('static int pf_take('):s.index('static void pf_plan(')]
plan=s[s.index('static void pf_plan('):s.index('// fread goes through')]
trace=[]
if args.trace:
 trace=[tuple(map(int,x)) for x in re.findall(r'\[io\] frame (\d+) h=1018 pos=(\d+) n=(\d+).*?ahead=(\d+)',args.trace.read_text(errors='replace'))]
head=r'''
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#define PF_MIN 262144u
#define PF_MAX 1310720u
typedef struct{int value;} SDL_atomic_t;
typedef int SDL_Thread;
static int SDL_AtomicGet(SDL_atomic_t *a){return a->value;}
#define SDL_memcpy memcpy
static void SDL_Delay(unsigned n){(void)n;abort();}
'''+file+r'''
static uint8_t storage[PF_MAX],output[PF_MAX+16];
static unsigned starts;
static uint8_t byte_at(uint64_t p){return (uint8_t)((p*131u)^(p>>7)^(p>>16));}
static void pf_start(File *f,long p,uint32_t n){
 assert(n<=PF_MAX);starts++;f->th=(SDL_Thread*)(uintptr_t)1;f->pfpos=p;f->pfwant=n;f->pf=storage;f->pfgot.value=n;f->pfdone.value=1;
 for(unsigned i=0;i<n;i++)storage[i]=byte_at((uint64_t)p+i);
}
'''
tail=r'''
static void check(void){
 File f={0};starts=0;pf_plan(&f,0,PF_MIN);assert(!starts&&!f.lastn);
 f.ro=1;pf_plan(&f,0,PF_MIN-1);pf_plan(&f,0,PF_MAX+1);assert(!starts&&!f.lastn);
 pf_plan(&f,10000,PF_MIN);pf_plan(&f,5000000,PF_MIN);assert(f.pfwant==PF_MIN); // Non-archive policy unchanged.
 const char *e=getenv("SH4_PREFETCH_GUARD");int enabled=e&&!strcmp(e,"1");
#ifdef __vita__
 if(!e)enabled=1;
#endif
 unsigned sizes[]={PF_MIN,614400,PF_MAX-4096,PF_MAX-2048,PF_MAX};
 for(unsigned i=0;i<sizeof sizes/sizeof sizes[0];i++){
  memset(&f,0,sizeof f);f.ro=1;f.pfreuse=1;unsigned n=sizes[i];starts=0;pf_plan(&f,10000,n);assert(!starts);
  pf_plan(&f,5000000,n);assert(starts==1&&f.pfpos==5000000+(long)n-4096);
  unsigned extra=enabled?(PF_MAX-n<4096?PF_MAX-n:4096):0;assert(f.pfwant==n+extra);
 }
 memset(&f,0,sizeof f);f.ro=1;f.pfreuse=1;pf_plan(&f,10000,PF_MIN);pf_plan(&f,4000000,PF_MIN+123);pf_plan(&f,10000,PF_MIN);
 assert(f.pfpos==4000000&&f.pfwant==PF_MIN+123); // learned exact range is never expanded
 for(unsigned i=0;i<128;i++){
  memset(&f,0,sizeof f);pf_start(&f,1000000,PF_MIN+4096);
  unsigned offset=(i&1)?2048:0,n=(i&2)?PF_MIN+4096:PF_MIN;
  if(i&4)f.pfgot.value=PF_MIN-1;if(i&8)f.th=NULL;
  memset(output,0xa5,sizeof output);
  int want=f.th && (uint64_t)offset+n<=f.pfwant && offset+n<=(unsigned)f.pfgot.value;
  assert(pf_take(&f,f.pfpos+offset,output,n)==want);
  for(unsigned j=0;j<sizeof output;j++)assert(output[j]==(want&&j<n?byte_at(1000000ull+offset+j):0xa5));
 }
 puts("Planner bounds, learned exact ranges, read-only policy, partial EOF and 128 real range copies pass");
}
'''
tracecode=''
if trace:
 tracecode='static const struct T{unsigned frame;long pos;unsigned n,observed;} t[]={'+','.join('{%u,%ld,%u,%u}'%x for x in trace)+'};\n'
 tracecode+=r'''
static void replay(void){File f={0};f.ro=1;f.pfreuse=1;unsigned hits=0,observed=0;
 for(unsigned i=0;i<sizeof t/sizeof t[0];i++){
  int hit=f.th && t[i].pos>=f.pfpos && (uint64_t)(t[i].pos-f.pfpos)+t[i].n<=f.pfwant;
  if(hit){assert(pf_take(&f,t[i].pos,output,t[i].n));hits++;}
  observed+=t[i].observed;
  printf("frame=%u best_ready_hit=%d observed_hit=%u\n",t[i].frame,hit,t[i].observed);
  pf_plan(&f,t[i].pos,t[i].n);
 }printf("TRACE requests=%u observed_hits=%u potential_ready_hits=%u\n",(unsigned)(sizeof t/sizeof t[0]),observed,hits);
}
'''
with tempfile.TemporaryDirectory() as td:
 p=Path(td);exe=p/'test.exe';(p/'test.c').write_text(head+take+plan+tail+tracecode+'\nint main(void){check();'+('replay();' if trace else '')+'return 0;}\n')
 subprocess.run([str(r.parent/'tools/mingw64/bin/gcc.exe'),'-std=c11','-O2','-Wall','-Wextra','-Wno-misleading-indentation',str(p/'test.c'),'-o',str(exe)],check=True)
 for vita in (False,True):
  subprocess.run([str(r.parent/'tools/mingw64/bin/gcc.exe'),'-std=c11','-O2','-Wall','-Wextra','-Wno-misleading-indentation']+(['-D__vita__'] if vita else [])+[str(p/'test.c'),'-o',str(exe)],check=True)
  for enabled in ('0','1',None,'invalid'):
   env={**os.environ};env.pop('SH4_PREFETCH_GUARD',None)
   if enabled is not None:env['SH4_PREFETCH_GUARD']=enabled
   print('VITA POLICY',vita,'GUARD',enabled,flush=True);subprocess.run([str(exe)],env=env,check=True)
