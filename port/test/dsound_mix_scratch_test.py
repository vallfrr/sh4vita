"""Compare production mixer PCM/state with its distributed predecessor; inject OOM."""
from pathlib import Path
import tempfile,subprocess
r=Path(__file__).resolve().parents[2];s=(r/'port/src/dx/dsound.c').read_text()
old=(r/'port/test/fixtures/dsound_mix_before_scratch.inc').read_text()
def mixer(text):
 a=text.index('static void mix(');z=text.find('static void audio_start(',a);return text[a:z if z>=0 else len(text)]
structs=s[s.index('typedef struct DSBuf {'):s.index('static SDL_mutex *mx;')]
helpers=s[s.index('static float db_gain('):s.index('static void mix(')]
head=r'''
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#define OUT_RATE 48000
typedef struct{uint32_t id,refs;}ComObj;typedef int SDL_mutex;
typedef uint8_t Uint8;typedef uint32_t Uint32;
'''+structs+r'''
static SDL_mutex *mx;static DSBuf *bufs;
static struct{float pos[3],front[3],top[3],rolloff,dist;}L={{0,0,0},{0,0,1},{0,1,0},1,1};
static uint8_t memory[1<<16],*g_mem=memory;
static unsigned allocs,frees,fail_alloc,locks,movie_calls,events[128],nevents;
static void *test_calloc(size_t n,size_t k){allocs++;return fail_alloc?NULL:calloc(n,k);}
static void test_free(void *p){frees++;free(p);}
static int rt_cpu_profile_thread(const char *name){assert(!strcmp(name,"audio"));return 1;}
static void SDL_LockMutex(SDL_mutex *m){(void)m;locks++;}
static void SDL_UnlockMutex(SDL_mutex *m){(void)m;assert(locks);locks--;}
static Uint32 SDL_GetTicks(void){return 100;}
static void rt_log(const char *fmt,...){(void)fmt;}
static void w32_event_set(uint32_t ev,int v){assert(v==1&&nevents<128);events[nevents++]=ev;}
void movie_audio_mix(float *p,int n){movie_calls++;for(int i=0;i<n*2;i++)p[i]+=(i%17-8)*0.001f;}
#define MIX_FRAMES 1024u
static float mix_acc[MIX_FRAMES*2];
#define calloc test_calloc
#define free test_free
'''
test=r'''
static DSBuf seed[3],actual[3],reference[3];static uint8_t before[20000],after[20000];
static void setup(unsigned mode){
 memset(seed,0,sizeof seed);
 for(unsigned i=0;i<sizeof memory;i++)memory[i]=(uint8_t)(i*73u+(i>>4));
 for(unsigned i=0;i<3;i++){
  DSBuf *b=seed+i;b->mem=256+i*8192;b->bits=(mode+i)&1?8:16;b->ch=(mode+i)&2?2:1;b->align=b->ch*b->bits/8;
  b->size=128*b->align;b->rate=11025u<<i;b->freq=(mode&4)?44100:0;b->playing=1;b->looping=i!=2;
  b->volume=i==2?-10000:-(int32_t)(i*600);b->pan=i==1?-300:200;b->pos=(mode&8)?127.5:i*0.2;
  b->is3d=mode&1;b->mode=mode%3;b->mind=1;b->maxd=20;b->p3[0]=i+1;b->p3[2]=2;
  b->nnotify=3;b->notify[0].off=0;b->notify[1].off=b->align*127;b->notify[2].off=UINT32_MAX;
  for(unsigned k=0;k<3;k++)b->notify[k].ev=10*i+k+1;
 }
}
static void run(int len){
 memcpy(reference,seed,sizeof seed);for(unsigned i=0;i<3;i++)reference[i].next=i<2?reference+i+1:NULL;
 bufs=reference;memset(before,0xa5,sizeof before);nevents=movie_calls=allocs=frees=0;original_mix(NULL,before+8,len);
 unsigned expected_events[128],expected_n=nevents;memcpy(expected_events,events,nevents*sizeof *events);assert(allocs==1&&frees==1&&!locks&&movie_calls==1);
 memcpy(actual,seed,sizeof seed);for(unsigned i=0;i<3;i++)actual[i].next=i<2?actual+i+1:NULL;
 bufs=actual;memset(after,0xa5,sizeof after);nevents=movie_calls=allocs=frees=0;candidate_mix(NULL,after+8,len);
 assert(!memcmp(before,after,sizeof before));assert(nevents==expected_n&&!memcmp(events,expected_events,nevents*sizeof *events));
 assert(!locks&&movie_calls==1);assert(allocs==(len/4>1024));assert(frees==allocs);
 for(unsigned i=0;i<3;i++){actual[i].next=reference[i].next=NULL;assert(!memcmp(actual+i,reference+i,sizeof *actual));}
}
int main(void){
 int sizes[]={4,8,28,128,256,1024,2048,4096,4098,8192};unsigned cases=0;
 for(unsigned mode=0;mode<16;mode++)for(unsigned j=0;j<sizeof sizes/sizeof sizes[0];j++){setup(mode);run(sizes[j]);cases++;}
 setup(0);memcpy(actual,seed,sizeof seed);bufs=actual;for(unsigned i=0;i<3;i++)actual[i].next=i<2?actual+i+1:NULL;
 fail_alloc=1;allocs=frees=0;for(unsigned i=0;i<1000;i++){nevents=0;candidate_mix(NULL,after+8,4096);}assert(!allocs&&!frees&&!locks);
 DSBuf saved[3];memcpy(saved,actual,sizeof saved);memset(after,0xa5,sizeof after);nevents=movie_calls=0;
 candidate_mix(NULL,after+8,8192);assert(allocs==1&&!frees&&!locks&&!nevents&&!movie_calls&&!memcmp(saved,actual,sizeof saved));
 for(unsigned i=0;i<sizeof after;i++)assert(after[i]==(i>=8&&i<8200?0:0xa5));
 fail_alloc=0;setup(5);run(4096); // Recovery after a refused oversized callback.
 puts("160 PCM/state/notification comparisons exact; 1000 normal callbacks allocate nothing; oversized OOM produces bounded silence and recovers");
}
'''
code=head+helpers+mixer(old).replace('static void mix(', 'static void original_mix(')+mixer(s).replace('static void mix(', 'static void candidate_mix(')+test
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(code);exe=p/'test.exe'
 subprocess.run([str(r.parent/'tools/mingw64/bin/gcc.exe'),'-std=c11','-O2','-D__vita__',str(p/'test.c'),'-o',str(exe),'-lm'],check=True)
 subprocess.run([str(exe)],check=True)
