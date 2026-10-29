"""Trace DI slots through the actual GOG threshold/pair conversion routines."""
from pathlib import Path
import argparse,hashlib,os,shutil,subprocess
import tempfile
from save_write_test import HEAD
root=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--exe',type=Path,default=root/'build/pc/game/SILENT HILL 4.exe');args=parser.parse_args()
assert hashlib.sha256(args.exe.read_bytes()).hexdigest()=='cf7081a6c78dbe51f6dc2e1c9bd978d8486f1d7b59d7a3b88e027c9b39f21dd0'
names={'F_00406f50_orig','F_00406c50','F_004067c0','F_00407480','F_00553d20'}
bodies={}
for path in (root/'port/gen').glob('x86_f*.c'):
    text=path.read_text(encoding='utf-8')
    for name in names-bodies.keys():
        mark='void '+name+'(CPU *c) {'
        if mark in text:
            a=text.index(mark);bodies[name]=text[a:text.index('\n}',a)+2]
assert bodies.keys()==names
# Reuse only the checksum-independent PE loader, no save transport fixtures.
loader=HEAD[HEAD.index('static uint32_t word('):HEAD.index('static CPU start(')]
head=r'''
#include "rt.h"
#include "x86_funcs.h"
#include <assert.h>
#include <stdlib.h>
uint8_t *g_mem;uint32_t g_mem_size=32u<<20;
void F_00406920(CPU *c) { c->eax=0x10000;c->esp+=4; }
'''
tail=r'''
static void convert(int x,int y,int rx,int ry) {
 uint8_t *mb=g_mem;memset(mb+0x10000,0,0x300);memset(mb+0x11000,0,16);
 int values[4]={x,y,ry,rx};unsigned codes[4]={0,4,8,0x14};
 for(int i=0;i<4;i++){
  CPU c={.ecx=0x10000,.esp=0x300000};W32(c.esp+4,(uint32_t)values[i]);W32(c.esp+8,0x11000+i*4);W32(c.esp+12,codes[i]);
  F_00406f50_orig(&c);assert(c.esp==0x300010);
 }
 CPU c={.ecx=0x10000,.esp=0x300000};F_004067c0(&c);assert(c.esp==0x300004);
 c=(CPU){.esp=0x300000};F_00553d20(&c);assert(c.esp==0x300004);
}
int main(int argc,char **argv) {
 assert(argc==2);load_image(argv[1]);uint8_t *mb=g_mem;
 // This is the exact slot selection in GOG 0x4074c0:
 // X/Y and Ry/Rx feed codes 0,4,8,0x14. Z/Rz are not read by that loop.
 int di[6]={0,0,900,0,0,900};
 convert(di[0],di[1],di[3],di[4]);
 assert(rt_f32(R32(0x1236c54))==0 && rt_f32(R32(0x1236c58))==0);
 convert(0,0,900,0);assert(rt_f32(R32(0x1236c54))>.99f && rt_f32(R32(0x1236c58))==0);
 convert(0,0,0,900);assert(rt_f32(R32(0x1236c54))==0 && rt_f32(R32(0x1236c58))>.99f);
 convert(0,0,-900,0);assert(rt_f32(R32(0x1236c54))<-.99f);
 convert(900,0,0,0);assert(rt_f32(R32(0x1236c44))>.99f && rt_f32(R32(0x1236c54))==0);
 convert(0,0,299,0);assert(rt_f32(R32(0x1236c54))==0);
 free(g_mem);puts("GOG pad contract: fixed Ry/Rx slots drive the second pair; TPS Z/Rz alone do not reach this consumer; +/-300 threshold conversion produces digital full-scale pairs");
}
'''
with tempfile.TemporaryDirectory(prefix='sh4-pad-axis-contract-') as tmp:
    p=Path(tmp);(p/'test.c').write_text(head+loader+'\n'.join(bodies.values())+tail,encoding='utf-8')
    cc=os.environ.get('CC') or shutil.which('gcc') or 'J:/tools/mingw64/bin/gcc.exe'
    subprocess.run([cc,'-std=gnu11','-O2','-I'+str(root/'port/src/rt'),'-I'+str(root/'port/gen'),str(p/'test.c'),'-lm','-o',str(p/'test.exe')],check=True)
    subprocess.run([str(p/'test.exe'),str(args.exe.resolve())],check=True)
