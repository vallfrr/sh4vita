"""Compare production cached callbacks against the stock file demux on real GOG films."""
from pathlib import Path
import subprocess
import tempfile
import os
import sys

root = Path(__file__).resolve().parents[2]
source = (root/'port/src/dx/movie.c').read_text()
a = source.index('static plm_buffer_t *movie_file(')
b = source.index('// Audio buffer refill:', a)
callbacks = source[a:b]
head = r'''
#define SDL_MAIN_HANDLED
#include <assert.h>
#include <stdio.h>
#define PL_MPEG_IMPLEMENTATION
#include "third_party/pl_mpeg/pl_mpeg.h"
#include "dx/movie_stream_cache.h"
typedef struct {MovieStream *stream;MovieCursor cursor[2];} Movie;
static unsigned compare_packet(plm_packet_t *a, plm_packet_t *b) {
 assert((a!=NULL)==(b!=NULL));if(!a)return 0;
 assert(a->type==b->type && a->pts==b->pts && a->length==b->length);
 assert(!memcmp(a->data,b->data,a->length));return 1;
}
'''
tail = r'''
int main(int argc,char **argv) {
 assert(argc>1 && !SDL_Init(0));unsigned total=0;
 for(int i=1;i<argc;i++) {
  Movie m={0};m.stream=movie_stream_open(argv[i]);assert(m.stream);
  plm_demux_t *reference[2],*cached[2];int ended[2]={0};
  for(int k=0;k<2;k++) {
   reference[k]=plm_demux_create(movie_file(argv[i]),TRUE);
   cached[k]=plm_demux_create(movie_input(&m,argv[i],k),TRUE);
   assert(plm_demux_get_duration(reference[k],PLM_DEMUX_PACKET_VIDEO_1)==plm_demux_get_duration(cached[k],PLM_DEMUX_PACKET_VIDEO_1));
  }
  unsigned n=0,seed=12345;
  while(!ended[0]||!ended[1]) {
   seed=seed*1664525u+1013904223u;int k=(seed>>24)&1;if(ended[k])k^=1;
   if(compare_packet(plm_demux_decode(reference[k]),plm_demux_decode(cached[k])))n++;
   else ended[k]=1;
  }
  for(int k=0;k<2;k++) {
   plm_demux_rewind(reference[k]);plm_demux_rewind(cached[k]);
   for(int j=0;j<40;j++)assert(compare_packet(plm_demux_decode(reference[k]),plm_demux_decode(cached[k])));
   plm_demux_destroy(reference[k]);plm_demux_destroy(cached[k]);
  }
  movie_stream_close(m.stream);total+=n;printf("%s: %u packets, payload/type/PTS/duration and rewind identical\n",argv[i],n);
 }
 printf("Total identical packets: %u\n",total);SDL_Quit();
}
'''
files = sys.argv[1:] or [str(p) for p in sorted((root/'build/pc/game/movie').glob('m_*.pac'))]
with tempfile.TemporaryDirectory() as tmp:
    folder=Path(tmp);src=folder/'demux.c';src.write_text(head+callbacks+tail)
    exe=folder/'demux.exe';sdl=root.parent/'tools/sdl2/SDL2-2.32.10/x86_64-w64-mingw32'
    subprocess.run([str(root.parent/'tools/mingw64/bin/gcc.exe'),'-O2','-I'+str(root/'port/src'),
                    '-I'+str(sdl/'include/SDL2'),str(src),'-L'+str(sdl/'lib'),'-lSDL2','-lm','-o',str(exe)],check=True)
    env=os.environ.copy();env['PATH']=str(sdl/'bin')+os.pathsep+env['PATH']
    subprocess.run([str(exe),*files],env=env,check=True)
