"""Production producer under a stalled consumer, then random producer/consumer interleavings."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
text = (root/'port/src/dx/movie.c').read_text()
start = text.index('static void hand_frame(Movie *m, plm_frame_t *f) {')
function = text[start:text.index('// BGRA conversion', start)]
head = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define OQ 5
typedef struct {unsigned width,height;uint8_t *data;} Plane;
typedef struct {Plane y,cb,cr;double time;} plm_frame_t;
typedef struct {void *lock,*osem;unsigned ohead,otail,odropped;int obusy,oq[OQ];uint8_t *yuv[OQ];plm_frame_t of[OQ];} Movie;
static void SDL_LockMutex(void *p){(void)p;}
static void SDL_UnlockMutex(void *p){(void)p;}
static void SDL_SemPost(void *p){(void)p;}
'''
tail = r'''
int main(void) {
 Movie m={0};m.obusy=-1;uint8_t slots[OQ][384],data[384],held[384];
 for(int i=0;i<OQ;i++)m.yuv[i]=slots[i];
 plm_frame_t f={.y={16,16,data},.cb={8,8,data+256},.cr={8,8,data+320}};
 memset(data,17,sizeof data);hand_frame(&m,&f);
 int r=m.oq[m.ohead++%OQ];m.obusy=r;memcpy(held,m.yuv[r],sizeof held);
 for(int i=0;i<1000;i++) {memset(data,i,sizeof data);f.time=i+1;hand_frame(&m,&f);assert(!memcmp(held,m.yuv[r],sizeof held));}
 m.obusy=-1;unsigned seed=1;double last=-1;
 for(int i=0;i<100000;i++) {
  seed=seed*1664525u+1013904223u;
  if((seed&7)<5) {memset(data,i,sizeof data);f.time=1001+i;hand_frame(&m,&f);}
  else if(m.ohead!=m.otail) {
   r=m.oq[m.ohead++%OQ];m.obusy=r;assert(m.of[r].time>last);last=m.of[r].time;
   memcpy(held,m.yuv[r],sizeof held);
   for(int j=0;j<7;j++){f.time=1001+i+(j+1)/10.0;hand_frame(&m,&f);assert(!memcmp(held,m.yuv[r],sizeof held));}
   m.obusy=-1;
  }
  assert(m.otail-m.ohead<=OQ-2);
 }
 puts("Production FIFO: stalled in-flight slot never overwritten, output order preserved, bounded queue across 100000 interleavings");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    folder = Path(tmp)
    source = folder/'fifo.c'; source.write_text(head+function+tail)
    exe = folder/'fifo.exe'
    subprocess.run([str(root.parent/'tools/mingw64/bin/gcc.exe'), '-O2', str(source), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
