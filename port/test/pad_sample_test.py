"""Production physical sampling: one coherent cache per frame and device."""
from pathlib import Path
import os, shutil, subprocess, tempfile
root=Path(__file__).resolve().parents[2]
test=r'''
#include "pad_sample.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static Sint16 values[2][4];
static unsigned masks[2], queries[2];
static int index_of(SDL_GameController *pad) { assert(pad==(void*)1 || pad==(void*)2);return pad==(void*)1?0:1; }
Sint16 SDL_GameControllerGetAxis(SDL_GameController *pad,SDL_GameControllerAxis axis) {
 int i=index_of(pad);assert(axis<4);++queries[i];return values[i][axis];
}
Uint8 SDL_GameControllerGetButton(SDL_GameController *pad,SDL_GameControllerButton button) {
 int i=index_of(pad);++queries[i];return (masks[i]>>button)&1u;
}
int main(void) {
 Sh4PadSample a={0},b={0};SDL_GameController *first=(void*)1,*second=(void*)2;
 values[0][0]=-32768;values[0][1]=32767;values[0][2]=12345;values[0][3]=-12;
 masks[0]=(1u<<SDL_CONTROLLER_BUTTON_A)|(1u<<SDL_CONTROLLER_BUTTON_DPAD_LEFT);
 sh4_pad_sample(&a,first,0);Sh4PadSample saved=a;
 assert(a.axes[0]==-32768 && a.axes[1]==32767 && a.axes[2]==12345 && a.axes[3]==-12 && a.buttons==masks[0]);
 memset(values[0],0,sizeof values[0]);masks[0]=0;
 for(int i=0;i<100;i++)sh4_pad_sample(&a,first,0);
 assert(!memcmp(&a,&saved,sizeof a) && queries[0]==4+SDL_CONTROLLER_BUTTON_MAX);
 sh4_pad_sample(&b,second,0);assert(queries[1]==4+SDL_CONTROLLER_BUTTON_MAX && !b.buttons);
 sh4_pad_sample(&a,first,1);assert(!a.buttons && !a.axes[0] && queries[0]==2*(4+SDL_CONTROLLER_BUTTON_MAX));
 // Counter wrap and explicit invalidation after a modal both force fresh input.
 sh4_pad_sample(&a,first,~0u);sh4_pad_sample(&a,first,0);masks[0]=1u<<SDL_CONTROLLER_BUTTON_B;a.valid=0;
 sh4_pad_sample(&a,first,0);assert(a.buttons==masks[0] && queries[0]==5*(4+SDL_CONTROLLER_BUTTON_MAX));
 puts("pad sampling: same-frame immutable axes/buttons, independent devices, counter wrap and modal invalidation passed");
}
'''
with tempfile.TemporaryDirectory(prefix='sh4-pad-sample-') as tmp:
    p=Path(tmp);(p/'test.c').write_text(test,encoding='utf-8')
    cc=os.environ.get('CC') or shutil.which('gcc') or 'J:/tools/mingw64/bin/gcc.exe'
    subprocess.run([cc,'-std=gnu11','-O2','-Wall','-Wextra','-Werror','-I'+str(root/'port/src/dx'),
                    '-I'+str(root.parent/'tools/sdl2/SDL2-2.32.10/x86_64-w64-mingw32/include/SDL2'),
                    str(p/'test.c'),'-o',str(p/'test.exe')],check=True)
    subprocess.run([str(p/'test.exe')],check=True)
