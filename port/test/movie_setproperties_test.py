"""Exercise production sample allocation, resizing and failure paths."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
src = (root/'port/src/dx/movie.c').read_text(encoding='utf-8')
start = src.index('M(al, SetProperties) {')
fn = src[start:src.index('\nM(al, GetProperties)', start)]
head = r'''
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#define NSAMP 3
#define MK_SAMP 1
#define HR_OK 0
#define HR_FAIL 0x80004005u
typedef struct { uint32_t guest; } Com;
typedef struct Movie Movie;
typedef struct { Com com; Movie *m; uint32_t buf,size; int busy; } Samp;
struct Movie { Samp *samp[NSAMP]; uint32_t buf_size; };
typedef struct { uint32_t args[4],result; } CPU;
static Movie movie; static uint8_t mem[1024]; static unsigned next=128,live,attempt,fail_at;
static int sm_vt[19];
#define HOSTFN uint8_t *mb=mem; (void)mb
#define MV Movie *m=&movie
#define M(a,b) void a##_##b(CPU *c)
#define ARG(n) c->args[n]
#define R32(p) (*(uint32_t *)(mem+(p)))
#define W32(p,v) (R32(p)=(v))
#define STDRET(n,v) do { c->result=(v);return; } while(0)
static uint32_t gheap_calloc(uint32_t n) {assert(n);if (++attempt==fail_at) return 0;live++;return ++next;}
static void gheap_free(uint32_t p) { if(p)live--; }
static void com_wrap(Com *p,int k,void *v,int n,const char *s) {(void)k;(void)v;(void)n;(void)s;p->guest=++next;}
#include "movie_sample_reserve.h"
'''
test = r'''
int main(void) {
 CPU c={0};c.args[2]=16;c.args[3]=32;W32(20,100);W32(24,1);
 al_SetProperties(&c);assert(c.result==0 && live==3 && movie.buf_size==100);
 uint32_t old[3];for(int i=0;i<3;i++){old[i]=movie.samp[i]->buf;assert(movie.samp[i]->size==100);}
 W32(20,200);attempt=0;fail_at=2;al_SetProperties(&c);
 assert(c.result==0x8007000e && movie.buf_size==100 && live==3);
 for(int i=0;i<3;i++)assert(movie.samp[i]->buf==old[i] && movie.samp[i]->size==100);
 movie.samp[1]->busy=1;attempt=0;fail_at=0;al_SetProperties(&c);
 assert(c.result==HR_FAIL && attempt==0 && movie.buf_size==100);
 movie.samp[1]->busy=0;al_SetProperties(&c);
 assert(c.result==0 && live==3 && movie.buf_size==200 && R32(36)==200);
 for(int i=0;i<3;i++)assert(movie.samp[i]->buf!=old[i] && movie.samp[i]->size==200);
 puts("movie allocator safely handles grow, allocation failure and busy samples");
}
'''
with tempfile.TemporaryDirectory(prefix='sh4-movie-props-') as tmp:
    code=Path(tmp)/'test.c'; exe=Path(tmp)/'test.exe'
    code.write_text(head+fn+test,encoding='utf-8')
    subprocess.run(['gcc','-std=c11','-O2','-I'+str(root/'port/src/dx'),str(code),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
