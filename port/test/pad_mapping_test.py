"""Preserve the previous Vita button contract and test experimental chords.

This checks mapping/state invariants, not physical pad comfort or game action
eligibility; actual item-info and evasion still need gameplay qualification.
"""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
code = r'''
#include "pad_mapping.h"
#include "pad_context.h"
#include <assert.h>
#include <stdio.h>
#define B(name) (1u << SDL_CONTROLLER_BUTTON_##name)
static unsigned fresh(unsigned raw,int menu,int combos) {
 Sh4PadButtons state={0};return sh4_pad_buttons(&state,raw,menu,combos);
}
static unsigned reference(unsigned raw,int menu) {
 unsigned out=0;
 if(raw&B(X))out|=1;
 if(raw&B(B))out|=2;
 if(raw&B(A))out|=4;
 if(raw&B(Y))out|=8;
 if(raw&B(LEFTSHOULDER))out|=menu?64:16;
 if(raw&B(RIGHTSHOULDER))out|=menu?128:32;
 if(raw&B(START))out|=256;
 if(raw&B(BACK))out|=2048;
 return out;
}
int main(void) {
 assert(SDL_CONTROLLER_BUTTON_MAX<32);
 for(unsigned raw=0;raw<(1u<<SDL_CONTROLLER_BUTTON_MAX);raw++) {
  assert(fresh(raw,0,0)==reference(raw,0));
  assert(fresh(raw,1,0)==reference(raw,1));
  assert(fresh(raw,1,1)==reference(raw,1));
  assert(fresh(raw,SH4_PAD_POINTER,1)==reference(raw,0)); // phone/keypad cancel stays usable
  unsigned mapped=fresh(raw,0,1);
  assert(!(mapped&~4095u));
  assert((mapped&256u)==(reference(raw,0)&256u)); // pause remains reachable
  assert((mapped&2048u)==(reference(raw,0)&2048u)); // album remains reachable
  assert((mapped&12u)==(reference(raw,0)&12u)); // confirm and inventory unchanged
 }
 assert(fresh(B(LEFTSHOULDER)|B(X),0,1)==512);
 assert(fresh(B(RIGHTSHOULDER)|B(B),0,1)==(1024|32));
 assert(!fresh(B(LEFTSHOULDER)|B(X)|B(RIGHTSHOULDER)|B(B),0,1));
 assert(fresh(B(LEFTSHOULDER),0,1)==16);
 assert(fresh(B(RIGHTSHOULDER),0,1)==32);
 assert(fresh(B(X),0,1)==1);
 assert(fresh(B(B),0,1)==2);
 Sh4PadButtons ambiguity={0};
 assert(!sh4_pad_buttons(&ambiguity,B(LEFTSHOULDER)|B(X)|B(RIGHTSHOULDER)|B(B),0,1));
 assert(!sh4_pad_buttons(&ambiguity,B(RIGHTSHOULDER)|B(B),0,1));
 assert(!sh4_pad_buttons(&ambiguity,0,0,1));
 assert(sh4_pad_buttons(&ambiguity,B(RIGHTSHOULDER)|B(B),0,1)==(1024|32));
 for(int order=0;order<2;order++) {
  Sh4PadButtons state={0};
  assert(sh4_pad_buttons(&state,B(LEFTSHOULDER)|B(X),0,1)==512);
  unsigned remain=order?B(LEFTSHOULDER):B(X);
  assert(!sh4_pad_buttons(&state,remain,0,1)); // no map/rear-camera roll-off
  assert(!sh4_pad_buttons(&state,remain,1,1)); // no option-page action on transition
  assert(!sh4_pad_buttons(&state,0,0,1));
  assert(sh4_pad_buttons(&state,B(X),0,1)==1);
  state=(Sh4PadButtons){0};
  assert(sh4_pad_buttons(&state,B(RIGHTSHOULDER)|B(B),0,1)==(1024|32));
  remain=order?B(RIGHTSHOULDER):B(B);
  assert(sh4_pad_buttons(&state,remain,0,1)==(order?32:0));
  assert(!sh4_pad_buttons(&state,remain,1,1)); // no cancel/page action on transition
  assert(!sh4_pad_buttons(&state,0,0,1));
  assert(sh4_pad_buttons(&state,B(B),0,1)==2);
 }
 Sh4PadContext s={0};
 for(unsigned f=0;f<6;f++) {
  int expected=f==5;
  assert(sh4_pad_cursor(&s,f,1)==expected);
  for(int repeat=0;repeat<100;repeat++)assert(sh4_pad_cursor(&s,f,repeat&1)==expected);
 }
 for(unsigned f=6;f<10;f++)assert(sh4_pad_cursor(&s,f,1));
 for(unsigned f=10;f<19;f++)assert(sh4_pad_cursor(&s,f,0)==(f<18));
 assert(!sh4_pad_cursor(&s,19,0));
 for(unsigned f=0xfffffffau;f!=6;f++)sh4_pad_cursor(&s,f,0);
 assert(!s.cursor && !s.score);
 puts("pad: exhaustive physical masks preserve old/default and option-page controls; chords are exclusive; cursor hysteresis advances once per frame including wraparound");
}
'''
compiler = os.environ.get('CC') or shutil.which('gcc') or 'J:/tools/mingw64/bin/gcc.exe'
with tempfile.TemporaryDirectory(prefix='sh4-pad-mapping-') as tmp:
    path = Path(tmp)
    (path / 'test.c').write_text(code, encoding='utf-8')
    subprocess.run([compiler, '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-I' + str(root / 'port/src/dx'),
                    '-I' + str(root.parent / 'tools/sdl2/SDL2-2.32.10/x86_64-w64-mingw32/include/SDL2'),
                    str(path / 'test.c'), '-o', str(path / 'test.exe')], check=True)
    subprocess.run([str(path / 'test.exe')], check=True)
