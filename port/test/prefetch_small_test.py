"""Existing prefetch jobs can serve short immutable-archive tails, opt-in."""
from pathlib import Path
import os,subprocess,tempfile
r=Path(__file__).resolve().parents[2];s=(r/'port/src/win32/kernel32.c').read_text()
gate=s[s.index('        int use_pf = n >= PF_MIN;'):s.index('        // A short audio tail')]
take=s[s.index('static int pf_take('):s.index('static void pf_plan(')]
plan=s[s.index('static void pf_plan('):s.index('// fread goes through')]
code=r'''
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#define PF_MIN 262144u
#define PF_MAX 1310720u
typedef struct{int value;} SDL_atomic_t;
typedef struct{int ro,pfreuse;void *th;long pfpos;uint32_t pfwant;uint8_t *pf;SDL_atomic_t pfdone,pfgot;long lastpos;uint32_t lastn,nxi;struct{long key,pos;uint32_t n;}nx[8];} File;
static int SDL_AtomicGet(SDL_atomic_t *a){return a->value;}
static void SDL_Delay(int n){(void)n;abort();}
#define SDL_memcpy memcpy
static unsigned starts;
static void pf_start(File *f,long pos,uint32_t n){assert(n<=PF_MAX);f->th=(void*)1;f->pfpos=pos;f->pfwant=n;f->pfgot.value=n;f->pfdone.value=1;starts++;}
static int eligible(File *f,uint32_t n){
'''+gate+r'''
return use_pf;
}
'''+take+plan+r'''
static uint8_t input[PF_MIN+4096],output[PF_MIN+8192];
int main(void){
 const char *e=getenv("SH4_PREFETCH_SMALL");int enabled=e&&!strcmp(e,"1");
 for(unsigned i=0;i<sizeof input;i++)input[i]=(uint8_t)(i^(i>>7));
 unsigned sizes[]={0,1,65535,65536,131072,247808,PF_MIN-1,PF_MIN,PF_MIN+4096};unsigned cases=0;
 for(unsigned flags=0;flags<32;flags++)for(unsigned k=0;k<sizeof sizes/sizeof sizes[0];k++){
  File f={.ro=!!(flags&1),.pfreuse=!!(flags&2),.th=(flags&4)?NULL:(void*)1,.pfpos=10000,.pfwant=sizeof input,.pf=input,.pfdone={1},.pfgot={(flags&8)?247807:sizeof input}};
  uint32_t n=sizes[k],offset=(flags&16)?4096:0;
  int want=n>=PF_MIN || (enabled&&f.ro&&f.pfreuse&&n>=65536);assert(eligible(&f,n)==want);
  memset(output,0xa5,sizeof output);int hit=want&&pf_take(&f,f.pfpos+offset,output+8,n);
  int expected=want&&f.th&&(uint64_t)offset+n<=f.pfwant&&(uint64_t)offset+n<=(unsigned)f.pfgot.value;
  assert(hit==expected);
  for(unsigned j=0;j<sizeof output;j++){assert(output[j]==(hit&&j>=8&&j<8+n?input[offset+j-8]:0xa5));}
  cases++;
 }
 File stream={.ro=1,.pfreuse=1};long positions[]={196667392,197279744,197892096,198504448,199116800,199729152,200341504};
 for(unsigned cycle=0;cycle<3;cycle++)for(unsigned j=0;j<7;j++){
  uint32_t n=j==6?247808:614400;long pos=positions[j];
  int hit=eligible(&stream,n)&&stream.th&&pos>=stream.pfpos&&(uint64_t)(pos-stream.pfpos)+n<=stream.pfwant;
  if(j==6&&cycle)assert(hit==enabled);
  unsigned before=starts;pf_plan(&stream,pos,n);
  if(j==6&&enabled){assert(stream.lastpos==pos&&stream.lastn==n&&starts==before);}
 }
 printf("%u production eligibility/range copies and three real BGM loop layouts: tails remain learned without extra post-tail jobs; EOF and guards pass\n",cases);
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(code);exe=p/'test.exe'
 subprocess.run([str(r.parent/'tools/mingw64/bin/gcc.exe'),'-std=c11','-O2','-Wall','-Wextra','-Werror',str(p/'test.c'),'-o',str(exe)],check=True)
 for value in (None,'0','1','invalid'):
  env={**os.environ};env.pop('SH4_PREFETCH_SMALL',None)
  if value is not None:env['SH4_PREFETCH_SMALL']=value
  print('SMALL',value,flush=True);subprocess.run([str(exe)],check=True,env=env)
