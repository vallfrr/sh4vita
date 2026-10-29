"""The diagnostic audio clock must not change PCM, gain, wrapping or underruns."""
from pathlib import Path
import tempfile
import subprocess

r=Path(__file__).resolve().parents[2]
new=(r/'port/src/dx/movie.c').read_text()
old=(r/'build/room-audit/movie-fluidity-20261029/before/port/src/dx/movie.c').read_text()
def extract(text,name,end):
    a=text.index(name)
    return text[a:text.index(end,a)]
reference=extract(old,'void movie_audio_mix(','// ---------- calls into').replace('void movie_audio_mix(', 'void reference(')
actual=extract(new,'void movie_audio_mix(','// ---------- calls into')
flush=extract(new,'void movie_audio_flush(','// Called by the mixer')
head=r'''
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
static float g_ring[4096];static uint32_t g_ring_r,g_ring_w,g_movie_audio_played;
static int g_audio_gain_q8;static void *g_ring_lock=(void*)1;
static void *SDL_CreateMutex(void){return (void*)1;}
static void SDL_LockMutex(void *p){(void)p;}static void SDL_UnlockMutex(void *p){(void)p;}
'''
tail=r'''
int main(void){
 unsigned seed=1;
 for(unsigned n=0;n<10000;n++){
  for(int i=0;i<4096;i++){seed=seed*1664525u+1013904223u;g_ring[i]=(int)(seed%65536)-32768;}
  uint32_t start=(seed>>12)%2048*2;g_ring_r=start;g_ring_w=(start+(seed%2048)*2)%4096;
  g_audio_gain_q8=(int)(seed%1025)-512;
  float a[2048],b[2048];for(int i=0;i<2048;i++)a[i]=b[i]=i/16.0f;
  int frames=seed%1025;reference(a,frames);unsigned end=g_ring_r;
  g_ring_r=start;g_movie_audio_played=0;movie_audio_mix(b,frames);
  assert(g_ring_r==end&&!memcmp(a,b,sizeof a));
  assert(g_movie_audio_played==((end+4096-start)%4096)/2);
  movie_audio_flush();assert(g_ring_r==g_ring_w&&!g_movie_audio_played);
 }
 puts("10000 movie PCM/state comparisons exact; diagnostic clock counts consumed frames, flush resets it");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    p=Path(tmp);(p/'a.c').write_text(head+reference+actual+flush+tail)
    subprocess.run([str(r.parent/'tools/mingw64/bin/gcc.exe'),'-O2',str(p/'a.c'),'-o',str(p/'a.exe')],check=True)
    subprocess.run([str(p/'a.exe')],check=True)
