"""Differentially compare production SET encoder with its generic path."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
s=(root/'port/src/d3d8/prep.c').read_text();start=s.index('int prep_set(');end=s.index('\nvoid prep_bind',start)
fn=s[start:end]
head=r'''
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
static unsigned char device[512];static void *g_dev=device;
static uint32_t record[64];static unsigned commits;
#define R_SET 7
static uint32_t *rec_begin(int op,unsigned n) {assert(n<=64);record[0]=op;return record;}
static void rec_commit(unsigned n) {commits=n;}
'''
reference=r'''
static int reference(void *field,const void *src,size_t n) {
 if(!memcmp(field,src,n))return 0;
 memcpy(field,src,n);unsigned w=(n+3)/4,nw=2+w;
 uint32_t *r=rec_begin(R_SET,nw);r[1]=((uint32_t)((char*)field-(char*)g_dev)<<16)|(uint32_t)n;
 r[nw-1]=0;memcpy(r+2,src,n);rec_commit(nw);return 1;
}
'''
test=r'''
int main(void) {
 unsigned seed=1;unsigned char initial[512],expected[512],src[32];uint32_t rec[64];
 for(unsigned i=0;i<50000;i++) {
  for(unsigned j=0;j<512;j++){seed=1664525*seed+1013904223;initial[j]=seed>>24;}
  for(unsigned j=0;j<32;j++){seed=1664525*seed+1013904223;src[j]=seed>>24;}
  unsigned n=(i%3==0)?4:(i%31)+1,off=i%450;
  if(i%4==0)memcpy(src,initial+off,n);
  memcpy(device,initial,512);memset(record,0xaa,sizeof(record));commits=0;
  int a=reference(device+off,src,n);unsigned count=commits;memcpy(expected,device,512);memcpy(rec,record,sizeof(rec));
  memcpy(device,initial,512);memset(record,0xaa,sizeof(record));commits=0;
  int b=prep_set(device+off,src,n);
  assert(a==b && count==commits && !memcmp(expected,device,512) && !memcmp(rec,record,sizeof(rec)));
 }
 puts("50000 SET encodings and device states match generic reference");
}
'''
with tempfile.TemporaryDirectory(prefix='sh4-prep-scalar-') as d:
    code=Path(d)/'test.c';exe=Path(d)/'test.exe';code.write_text(head+fn+reference+test)
    subprocess.run(['gcc','-std=c11','-O2',str(code),'-o',str(exe)],check=True)
    import os
    subprocess.run([str(exe)],env={**os.environ,'SH4_PREP_SCALAR':'1'},check=True)
