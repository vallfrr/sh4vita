"""Exercise production Pause/Stop/decoder cleanup ordering and idempotence."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root/'port/src/dx/movie.c').read_text(encoding='utf-8')
start = source.index('static void movie_free_output(')
helpers = source[start:source.index('// Decode -> output:', start)]
start = source.index('M(ctl, Pause)')
methods = source[start:source.index('M(ctl, GetState)', start)]
head = r'''
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#define OQ 5
typedef struct {unsigned eax,esp;} CPU;
typedef int SDL_Thread;
typedef struct {uint64_t requests,reads,read_bytes,hit_bytes,io_ticks;} MovieStream;
typedef struct {void *plm,*aud,*adm;void *yuv[OQ];CPU *dec_cpu;
 SDL_Thread *thread,*othread,*athread;unsigned state,stop,renderer,ohead,otail,odropped;int obusy;MovieStream *stream;} Movie;
static Movie *current;static unsigned live,freed_cpus,joins,flushes,pauses,stops;
static int g_movie_w,g_movie_h;
static void *allocate(void){void *p=malloc(32);assert(p);live++;return p;}
static void release(void *p){
 if(!p)return;
 assert(current && !current->thread && !current->othread && !current->athread);
 assert(live);live--;free(p);
}
static void rt_free_cpu(CPU *p){if(p){freed_cpus++;release(p);}}
static void SDL_WaitThread(SDL_Thread *t,void *result){assert(t);(void)result;joins++;}
static void movie_audio_flush(void){flushes++;}
static void gcall(CPU *c,unsigned object,int slot,int nargs){
 (void)c;assert(object==77 && !nargs);
 if(slot==5)pauses++;else {assert(slot==4);stops++;}
}
static void plm_destroy(void *p){release(p);}
static void plm_audio_destroy(void *p){release(p);}
static void plm_demux_destroy(void *p){release(p);}
static void movie_stream_close(MovieStream *p){release(p);}
static uint64_t SDL_GetPerformanceFrequency(void){return 1000000;}
#define rt_log(...) ((void)0)
#define free release
#define M(a,b) static void a##_##b(CPU *c)
#define HOSTFN (void)c
#define MV Movie *m=current
#define HR_OK 0
#define STDRET(n,v) do{c->eax=(v);c->esp+=4*((n)+1);return;}while(0)
'''
tail = r'''
#undef free
int main(void){
 CPU caller={0};
 for(unsigned i=0;i<1000;i++){
  Movie m={0};current=&m;
  m.plm=allocate();m.aud=allocate();m.adm=allocate();m.dec_cpu=allocate();
  for(unsigned j=0;j<OQ;j++)m.yuv[j]=allocate();
  m.thread=m.othread=m.athread=(SDL_Thread*)1;m.renderer=77;m.state=2;
  m.ohead=3;m.otail=5;m.obusy=4;
  ctl_Pause(&caller);assert(live==9&&m.state==1&&m.dec_cpu&&m.yuv[4]);
  ctl_Stop(&caller);assert(live==3&&!m.state&&!m.dec_cpu&&m.obusy==-1&&!m.ohead&&!m.otail);
  for(unsigned j=0;j<OQ;j++)assert(!m.yuv[j]);
  assert(m.plm&&m.aud&&m.adm); // Position/duration state is retained until graph cleanup.
  unsigned joined=joins,released=freed_cpus;
  ctl_Stop(&caller);assert(live==3&&joins==joined&&freed_cpus==released);
  movie_free_decoders(&m);assert(!live&&!m.plm&&!m.aud&&!m.adm);
  movie_free_decoders(&m);assert(!live&&freed_cpus==released);
 }
 assert(joins==3000&&freed_cpus==1000&&pauses==1000&&stops==1000&&flushes==2000);
 puts("1000 production movie Pause/Stop/cleanup cycles: join-before-free, CPU and 5 YUV slots released once, Pause retains buffers, repeated cleanup safe; decoder position/duration state retained");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    folder = Path(tmp)
    src = folder/'test.c'
    src.write_text(head+helpers+methods+tail, encoding='utf-8')
    exe = folder/'test.exe'
    subprocess.run([str(root.parent/'tools/mingw64/bin/gcc.exe'), '-std=c11', '-O2',
                    str(src), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
