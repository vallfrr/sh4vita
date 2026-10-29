"""Exercise the production post-dialog gate with independent physical devices."""
from pathlib import Path
import os, shutil, subprocess, tempfile
root = Path(__file__).resolve().parents[2]
source = (root/'port/src/dx/dinput.c').read_text(encoding='utf-8')
start = source.index('static int input_gate(')
function = source[start:source.index('\nM(dev, GetDeviceState)', start)]
head = r'''
#include "rt.h"
#include "input_gate.h"
#include <assert.h>
typedef unsigned char Uint8;
typedef int SDL_GameControllerButton;
typedef int SDL_GameControllerAxis;
#define SDL_CONTROLLER_BUTTON_MAX 21
typedef struct { int type; void *gc; int32_t rmin[8],rmax[8]; unsigned neutral_generation; int await_neutral; struct { int valid; } sample; } DIDev;
static Uint8 keys[512], buttons[21];
static int axes[4], mouse, queries;
int g_camera_pad_x, g_camera_pad_y, g_camera_pad_active;
const Uint8 *SDL_GetKeyboardState(int *n) { ++queries;*n=512;return keys; }
unsigned SDL_GetMouseState(int *x,int *y) { (void)x;(void)y;++queries;return mouse; }
int SDL_GameControllerGetButton(void *gc,int i) { assert(gc);++queries;return buttons[i]; }
int SDL_GameControllerGetAxis(void *gc,int i) { assert(gc);++queries;return axes[i]; }
'''
tail = r'''
int main(void) {
 uint8_t memory[256], *mb=memory; DIDev k={.type=0}, p={.type=2,.gc=(void*)1}, m={.type=1};
 for(int i=0;i<8;i++){p.rmin[i]=-1000;p.rmax[i]=1000;}
 assert(!input_gate(&p,mb,0,80) && !queries);
 keys[40]=1;buttons[0]=1;mouse=1;g_camera_pad_active=1;rt_input_neutralize();
 assert(input_gate(&k,mb,0,80) && k.await_neutral);
 memset(memory,0,sizeof memory);assert(input_gate(&p,mb,0,80));
 assert(!g_camera_pad_active && !g_camera_pad_x && !g_camera_pad_y);
 for(int i=0;i<6;i++)assert(R32(i*4)==0);
 for(int i=32;i<48;i+=4)assert(R32(i)==0xffffffffu);
 assert(input_gate(&m,mb,0,80) && m.await_neutral);
 keys[40]=0;assert(input_gate(&k,mb,0,80) && !k.await_neutral);
 assert(!input_gate(&k,mb,0,80));assert(input_gate(&p,mb,0,80) && p.await_neutral);
 buttons[0]=0;axes[0]=24000;assert(input_gate(&p,mb,0,80) && p.await_neutral);
 axes[0]=0;assert(input_gate(&p,mb,0,80) && !p.await_neutral);
 assert(!input_gate(&p,mb,0,80));mouse=0;assert(input_gate(&m,mb,0,80));assert(!input_gate(&m,mb,0,80));
 rt_input_neutralize();p.rmin[0]=0;p.rmax[0]=65535;
 memset(memory,0xa5,sizeof memory);assert(input_gate(&p,mb,8,4));assert(R32(8)==32767);
 assert(memory[7]==0xa5 && memory[12]==0xa5);assert(!input_gate(&p,mb,0,80));
 puts("input gate: independent releases, held buttons/axes, neutral POV, bounded writes and zero extra SDL queries during normal play passed");
}
'''
with tempfile.TemporaryDirectory(prefix='sh4-input-gate-') as tmp:
    p=Path(tmp);(p/'test.c').write_text(head+function+tail,encoding='utf-8')
    cc=os.environ.get('CC') or shutil.which('gcc') or 'J:/tools/mingw64/bin/gcc.exe'
    subprocess.run([cc,'-std=gnu11','-O2','-Wall','-Wextra','-Werror','-I'+str(root/'port/src/rt'),str(p/'test.c'),str(root/'port/src/rt/input_gate.c'),'-lm','-o',str(p/'test.exe')],check=True)
    subprocess.run([str(p/'test.exe')],check=True)
