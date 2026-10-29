"""Verify the production MMX-copy replacement with optional streamed delivery."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
s=(root/'port/src/rt/natives.c').read_text();a=s.index('void nat_memcpy_mmx(CPU *c) {');fn=s[a:s.index('\n}',a)+2]
head=r'''
#include "rt.h"
#include <stdlib.h>
#include <assert.h>
#include <string.h>
uint8_t *g_mem;int g_sound_transfer_on;static int delivery;
int rt_sound_archive_transfer(CPU*c,uint32_t d,uint32_t s,uint32_t n){
 (void)c;if(delivery==1){memmove(g_mem+d,g_mem+s,n);return 1;}
 if(delivery==2 && ((uint64_t)d+n<=s || (uint64_t)s+n<=d))g_mem[d]=0x66;
 return 0;
}
'''
test=r'''
int main(void){
 uint8_t expected[32768],initial[32768];g_mem=malloc(32768);assert(g_mem);uint8_t *mb=g_mem;unsigned r=1;
 for(unsigned t=0;t<10000;t++){
  for(unsigned i=0;i<32768;i++){r=r*1664525+1013904223;initial[i]=r>>24;}
  memcpy(g_mem,initial,sizeof initial);CPU c={0};c.esp=0x7000;c.eax=r;c.edx=t;c.ecx=~r;c.df=t%2;
  uint32_t src=512+t%7,dst=t%2?src+1:2048;int n=t%11==0?-1:t%257;
  W32(c.esp+4,dst);W32(c.esp+8,src);W32(c.esp+12,(uint32_t)n);
  memcpy(expected,g_mem,sizeof expected);if(n>0)memmove(expected+dst,expected+src,n);
  CPU before=c;before.esp+=4;g_sound_transfer_on=t%3;delivery=t%3;
  nat_memcpy_mmx(&c);assert(!memcmp(&c,&before,sizeof c)&&!memcmp(g_mem,expected,sizeof expected));
 }
 free(g_mem);puts("10000 production memcpy cases preserve CPU/memory with streamed delivery and partial-failure fallback");
}
'''
with tempfile.TemporaryDirectory(prefix='sh4-sound-memcpy-') as d:
 c=Path(d)/'test.c';exe=Path(d)/'test.exe';c.write_text(head+fn+test)
 subprocess.run(['gcc','-std=gnu11','-O2','-I'+str(root/'port/src/rt'),str(c),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
