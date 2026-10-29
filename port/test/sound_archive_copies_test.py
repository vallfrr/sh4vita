"""Compare generated stream-copy loops with the opt-in delivery hook."""
from pathlib import Path
import ast,subprocess,tempfile,re
root=Path(__file__).resolve().parents[2]
tree=ast.parse((root/'recomp/x86rec.py').read_text());node=next(n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='optimize_sound_archive_copies');scope={'re':re};exec(compile(ast.Module(body=[node],type_ignores=[]),'transform','exec'),scope)
loops=[]
for width in (1,4):
 loops.append(f'''{{ int32_t st=c->df?-{width}:{width}; if (!c->df && (uint32_t)(edi-esi) >= ecx*{width}u) {{ memmove(mb+edi, mb+esi, ecx*{width}u); esi+=ecx*{width}u; edi+=ecx*{width}u; ecx=0; }} else for (; ecx; ecx--) {{ W{width*8}(edi,R{width*8}(esi)); esi+=st; edi+=st; }} }}''')
# Four copies of each width are present in the original streaming function.
body='uint8_t *const mb = g_mem;\n'+('\n'.join(loops)+'\n')*4
transformed=scope['optimize_sound_archive_copies'](body).replace('#include "sound_archive_access.h"\n','',1)
try:scope['optimize_sound_archive_copies'](body.replace('memmove','memcpy',1))
except RuntimeError:pass
else:raise AssertionError('changed source must be rejected')
head=r'''
#include "rt.h"
#include "sound_archive_access.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
uint8_t *g_mem;int g_sound_transfer_on;static int deliver;
int g_sound_read_headers;
int rt_sound_archive_scalar(CPU*c,uint32_t s,void*out,uint32_t n){(void)c;memcpy(out,g_mem+s,n);return 1;}
int rt_sound_archive_transfer(CPU*c,uint32_t d,uint32_t s,uint32_t n){(void)c;if(!deliver)return 0;memmove(g_mem+d,g_mem+s,n);return 1;}
'''
def fun(name,code):return 'static void '+name+'(CPU*c,uint32_t *r){uint32_t edi=r[0],esi=r[1],ecx=r[2];'+code+'r[0]=edi;r[1]=esi;r[2]=ecx;}\n'
test=r'''
int main(void){
 uint8_t ref[8192],original[8192];g_mem=malloc(8192);assert(g_mem);unsigned rng=1;
 for(unsigned t=0;t<20000;t++) {
  for(unsigned i=0;i<8192;i++){rng=rng*1664525+1013904223;original[i]=rng>>24;}
  uint32_t in[3]={2048+(t%5),1024+(t%7),t%129},a[3],b[3];
  if(t%3==0)in[0]=in[1]+(t%8);
  CPU c={0};c.df=t%2;memcpy(a,in,sizeof a);memcpy(b,in,sizeof b);
  memcpy(g_mem,original,8192);stock(&c,a);memcpy(ref,g_mem,8192);
  g_sound_transfer_on=t%3;g_sound_read_headers=t%2;deliver=t%4;memcpy(g_mem,original,8192);native(&c,b);
  assert(!memcmp(a,b,sizeof a)&&!memcmp(ref,g_mem,8192));
 }
 puts("20000 generated copy cases retain memory/register effects with delivery, fallback, overlap and reverse direction");free(g_mem);
}
'''
with tempfile.TemporaryDirectory(prefix='sh4-stream-copies-') as d:
 c=Path(d)/'test.c';exe=Path(d)/'test.exe';c.write_text(head+fun('stock',body)+fun('native',transformed)+test)
 subprocess.run(['gcc','-std=gnu11','-O2','-I'+str(root/'port/src/rt'),str(c),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
