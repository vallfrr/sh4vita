"""Exercise production pad mapping and original guest window-camera input on the host."""
from pathlib import Path
import argparse,re,subprocess,tempfile,hashlib
ROOT=Path(__file__).resolve().parents[2]
def function(source,name):
 start=source.index('void '+name+'(CPU *c) {');end=source.index('\n}',start)+2;return source[start:end]
def main():
 p=argparse.ArgumentParser();p.add_argument('--cc',default='gcc');p.add_argument('--exe',type=Path,required=True);a=p.parse_args();assert hashlib.sha256(a.exe.read_bytes()).hexdigest()=='cf7081a6c78dbe51f6dc2e1c9bd978d8486f1d7b59d7a3b88e027c9b39f21dd0'

 # Locate functions independent of generated file grouping.
 names=['F_0054bae0_orig','F_004f86b0','F_004f8700','F_00554350_orig','F_00554370']
 bodies=[]
 for name in names:
  found=next(path.read_text(encoding='utf-8') for path in (ROOT/'port/gen').glob('x86_f*.c') if 'void '+name+'(CPU *c) {' in path.read_text(encoding='utf-8'))
  bodies.append(function(found,name))
 native=(ROOT/'port/src/rt/natives.c').read_text(encoding='utf-8');native=native[native.index('// Exterior-window'):native.index('// 0x526be0:')]
 inp=(ROOT/'port/src/dx/dinput.c').read_text(encoding='utf-8')
 axis=inp[inp.index('static int32_t axis('):inp.index('// Each device independently waits')]
 mapping=inp[inp.index('        #define SHAPE'):inp.index('        static int audit_pad')]
 calls=set(re.findall(r'\b(F_[0-9a-f]{8}(?:_orig)?)\(c\)', '\n'.join(bodies)))
 implemented=set(names)|{'F_00554350','F_0054cc10_orig','F_0055ae50','F_005230c0'}
 stubs='\n'.join('void '+name+'(CPU *c){c->eax=0;c->esp+=4;}' for name in sorted(calls-implemented))
 head=r'''
#include "rt.h"
#include "x86_funcs.h"
#include <assert.h>
#include <stdlib.h>
uint8_t *g_mem;uint32_t g_mem_size=32u<<20;
unsigned g_frame;
typedef int16_t Sint16;
typedef struct {int32_t rmin[8],rmax[8];int dead;} DIDev;
float rt_camera_scale(void){return 1.0f;}
void F_0055ae50(CPU *c){c->eax=30;c->esp+=4;}
void F_005230c0(CPU *c){c->eax=0x13000;c->esp+=4;}
void F_00554350_orig(CPU *c);
void F_00554350(CPU *c){F_00554350_orig(c);}
void F_0054cc10_orig(CPU *c){c->esp+=4;}
'''
 mapper='\nstatic void map(DIDev *self,uint32_t world,int tps,int menu,int cur,Sint16 lsx,Sint16 lsy,Sint16 rsx,Sint16 rsy){uint8_t *mb=g_mem;uint32_t p=0x14000;int stickmode=cur||menu;\n'+mapping+'\n}\n'
 test=r'''
int main(int argc,char **argv){
 assert(argc==2);load_image(argv[1]);
 uint8_t *mb=g_mem;DIDev dev={0};for(int i=0;i<8;i++){dev.rmin[i]=-1000;dev.rmax[i]=1000;}
 CPU c={.esp=0x300000,.esi=0x10000};g_frame=100;
 map(&dev,1,0,0,0,0,0,32767,0);assert((int32_t)R32(0x14000)>0 && R32(0x1400c)==0);
 // The original PC routine supports left/right and up/down with separate limits.
 W32(0x1236c54,rt_u32f(1));W32(0x1236c58,rt_u32f(-1));
 nat_window_camera(&c);
 assert(c.esp==0x300004 && rt_window_camera_active());
 assert(rt_f32(R32(0x10020))>0 && rt_f32(R32(0x10024))<0);
 map(&dev,1,0,0,0,0,0,32767,0);assert(R32(0x1400c)==R32(0x14000) && (int32_t)R32(0x1400c)>0);
 map(&dev,1,0,0,0,0,0,-32768,0);assert((int32_t)R32(0x1400c)<0);
 // Normal walking, third-person, menus and phone pointer keep their old mapping.
 map(&dev,2,1,0,0,0,0,32767,0);assert(R32(0x1400c)==0 && (int32_t)R32(0x14014)>0);
 map(&dev,1,0,1,0,0,0,32767,0);assert(R32(0x1400c)==0);
 map(&dev,1,0,0,1,0,0,32767,0);assert(R32(0x1400c)==0);
 c.esp=0x300000;nat_window_close(&c);assert(!rt_window_camera_active());
 map(&dev,1,0,0,0,0,0,32767,0);assert(R32(0x1400c)==0);
 c.esp=0x300000;W32(0x1236c54,rt_u32f(-1));W32(0x1236c58,rt_u32f(1));W32(0x10020,0);W32(0x10024,0);nat_window_camera(&c);
 assert(rt_f32(R32(0x10020))<0 && rt_f32(R32(0x10024))>0);
 g_frame+=3;assert(!rt_window_camera_active());
 puts("Original window camera accepts both axes; scoped pad mapping and exit/expiry checks passed.");free(g_mem);return 0;
}
'''
 loader=(ROOT/'port/test/language_test.py').read_text(encoding='utf-8');loader=loader[loader.index('static uint32_t word('):loader.index('static void language(')]
 with tempfile.TemporaryDirectory(prefix='sh4-window-input-') as folder:
  src=Path(folder)/'test.c';exe=Path(folder)/'test.exe';src.write_text(head+stubs+'\n'+native+'\n'+'\n'.join(bodies)+axis+mapper+loader+test,encoding='utf-8')
  subprocess.run([a.cc,'-std=gnu11','-O2','-I'+str(ROOT/'port/src/rt'),'-I'+str(ROOT/'port/gen'),str(src),'-lm','-o',str(exe)],check=True)
  subprocess.run([str(exe),str(a.exe.resolve())],check=True)
if __name__=='__main__':main()
