"""Follow physical chords through actual GOG binding/edge consumers, all presets."""
from pathlib import Path
import argparse,hashlib,os,re,shutil,struct,subprocess,tempfile
root=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--exe',type=Path,default=root/'build/pc/game/SILENT HILL 4.exe');args=parser.parse_args()
data=args.exe.read_bytes()
assert hashlib.sha256(data).hexdigest()=='cf7081a6c78dbe51f6dc2e1c9bd978d8486f1d7b59d7a3b88e027c9b39f21dd0'
pe=struct.unpack_from('<I',data,0x3c)[0];opt=pe+24;base=struct.unpack_from('<I',data,opt+28)[0];sections=opt+struct.unpack_from('<H',data,pe+20)[0]
image=bytearray(32<<20)
for i in range(struct.unpack_from('<H',data,pe+6)[0]):
    _,address,size,raw=struct.unpack_from('<4I',data,sections+40*i+8)
    assert raw+size<=len(data) and base+address+size<=len(image)
    image[base+address:base+address+size]=data[raw:raw+size]
names={'F_00406c50','F_004f84a0','F_00554250'}
bodies={}
for path in (root/'port/gen').glob('x86_f*.c'):
    text=path.read_text(encoding='utf-8')
    for name in names-bodies.keys():
        mark='void '+name+'(CPU *c) {'
        if mark in text:
            a=text.index(mark);bodies[name]=text[a:text.index('\n}',a)+2]
assert bodies.keys()==names
head=r'''
#include "rt.h"
#include "x86_funcs.h"
#include "pad_mapping.h"
#include "pad_script.h"
#include <stdlib.h>
#include <assert.h>
uint8_t *g_mem;
void F_004f8240(CPU *c) { c->eax=0;c->esp+=4; } // no cutscene/state blocking for this binding contract
'''
tail=r'''
static void bind_event(int button,int release) {
 uint8_t *mb=g_mem;CPU c={.ecx=0x1000,.esp=0x2000};
 W32(0x2004,0x3080+button*0x100);W32(0x2008,release);
 F_00406c50(&c);assert(c.esp==0x200c);
}
static unsigned query(unsigned action) {
 uint8_t *mb=g_mem;CPU c={.esp=0x2000};W32(0x2004,0);W32(0x2008,action);
 F_004f84a0(&c);assert(c.esp==0x2004);return c.eax;
}
static void sample(unsigned mapped) {
 uint8_t *mb=g_mem;W32(0x1010,0);
 for(int i=0;i<12;i++)if(mapped&(1u<<i))bind_event(i,0);
 W32(0x1236c30,R32(0x1010));W32(0x1236c34,R32(0x1010));
}
int main(int argc,char **argv) {
 assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);g_mem=malloc(32u<<20);assert(g_mem);
 assert(fread(g_mem,1,32u<<20,f)==32u<<20);fclose(f);uint8_t *mb=g_mem;
 for(unsigned preset=0;preset<3;preset++) {
  uint32_t source=R32(0x653d84+preset*4);memcpy(mb+0x107a1a0,mb+source,21*4);
  Sh4PadButtons state={0};
  unsigned info=(1u<<SDL_CONTROLLER_BUTTON_LEFTSHOULDER)|(1u<<SDL_CONTROLLER_BUTTON_X);
  unsigned scripted=sh4_pad_script_buttons("100:204:5",100,0,0x1fffffu);
  assert(scripted==info);
  sample(sh4_pad_buttons(&state,scripted,SH4_PAD_GAMEPLAY,1));
  assert(query(4)==1 && !query(2) && !query(1)); // Info, no Map or Cancel
  bind_event(9,1);assert(!R32(0x1010)); // release clears the original internal bit
  state=(Sh4PadButtons){0};
  unsigned evade=(1u<<SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)|(1u<<SDL_CONTROLLER_BUTTON_B);
  scripted=sh4_pad_script_buttons("100:402:5",100,0,0x1fffffu);
  assert(scripted==evade);
  sample(sh4_pad_buttons(&state,scripted,SH4_PAD_GAMEPLAY,1));
  assert(query(12)==1 && query(10)==1 && !query(1)); // Dodge with Aim, no Cancel/Run partner
  sample(sh4_pad_buttons(&state,1u<<SDL_CONTROLLER_BUTTON_B,SH4_PAD_GAMEPLAY,1));
  assert(!query(12) && !query(1)); // releasing R first does not become Cancel
 }
 assert(sh4_pad_script_buttons("100:204:5,110:402:3",105,8,0x1fffffu)==8);
 assert(sh4_pad_script_buttons("100:204:5,110:402:3",111,8,0x1fffffu)==(8|0x402));
 assert(sh4_pad_script_buttons("100:204:0",100,8,0x1fffffu)==8);
 assert(sh4_pad_script_buttons("100:ffffffff:5",100,8,0x1fffffu)==8);
 assert(sh4_pad_script_buttons("100:204:5oops",100,8,0x1fffffu)==8);
 free(g_mem);puts("GOG action chain: original binding events and edge consumers confirm Info=4, Dodge=12 with Aim=10, no Map/Cancel overlap in all three presets; animation/UI context remains separate");
}
'''
with tempfile.TemporaryDirectory(prefix='sh4-pad-actions-') as tmp:
    p=Path(tmp);(p/'image.bin').write_bytes(image);(p/'test.c').write_text(head+'\n'.join(bodies.values())+tail,encoding='utf-8')
    cc=os.environ.get('CC') or shutil.which('gcc') or 'J:/tools/mingw64/bin/gcc.exe'
    subprocess.run([cc,'-std=gnu11','-O2','-I'+str(root/'port/src/rt'),'-I'+str(root/'port/gen'),'-I'+str(root/'port/src/dx'),
                    '-I'+str(root.parent/'tools/sdl2/SDL2-2.32.10/x86_64-w64-mingw32/include/SDL2'),
                    str(p/'test.c'),'-lm','-o',str(p/'test.exe')],check=True)
    subprocess.run([str(p/'test.exe'),str(p/'image.bin')],check=True)
