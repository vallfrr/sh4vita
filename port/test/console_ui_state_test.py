"""Check UI draw isolation, bounded allocation failure and repeated release."""
from pathlib import Path
import subprocess,tempfile,shutil
root=Path(__file__).resolve().parents[2]
head=r'''
#define SDL_MAIN_HANDLED
#include "d3d8/console_ui.h"
#include <stdlib.h>
#include <assert.h>
int g_vita_bindings_open;uint8_t*g_mem;Device*g_dev;uint64_t g_mem_tex;static unsigned live,at=4096,fail,draws;
uint32_t gheap_alloc(uint32_t n){if(fail)return 0;unsigned p=at;at+=(n+15)&~15u;live++;assert(at<8u<<20);return p;}
void gheap_free(uint32_t p){assert(p&&live);if(!--live)at=4096;}
void rt_log(const char*s,...){(void)s;}
int prep_set(void*p,const void*q,size_t n){memcpy(p,q,n);return 1;}
void prep_bind(void){}
void prep_draw(Device*d,uint32_t prim,uint32_t first,uint32_t nv,uint32_t np,const uint8_t*ix,int i32,uint32_t mi,const uint8_t*up,uint32_t stride){
 assert(d==g_dev&&d->vs==0x144&&!d->ps&&prim==4&&!first&&nv>=6&&nv<=384&&nv%6==0&&np==nv/3&&!ix&&!i32&&!mi&&up>=g_mem&&stride==28);draws++;
}
void d3d_release(ComObj*o){Texture*t=(Texture*)o;assert(o->kind==K_TEXTURE&&o->refs==1);if(t->lv[0]->mem){g_mem_tex-=1024*512*4;gheap_free(t->lv[0]->mem);}free(t->lv[0]);free(t->lv);free(t);}
'''
tail=r'''
int main(int argc,char**argv){assert(argc==2);char env[2048];snprintf(env,sizeof env,"SH4_UI_ASSETS=%s",argv[1]);putenv(env);
 g_mem=calloc(1,8u<<20);Device d;memset(&d,0x49,sizeof d);d.vp.x=7;d.vp.y=11;d.vp.w=640;d.vp.h=362;d.ui_display=NULL;g_dev=&d;Device before=d;
 for(unsigned i=0;i<20;i++){
  assert(console_ui_begin());console_ui_sprite(UI_VITA,220,148,520,236);console_ui_sprite(UI_L,40,480,28,28);console_ui_line(40,20,60,40,0xffffffff);console_ui_end();
  assert(!memcmp(&d,&before,sizeof d));assert(live==2&&g_mem_tex==1024*512*4);
  console_ui_release();assert(!live&&!g_mem_tex&&!atlas&&!vertices&&!active);
 }
 g_vita_bindings_open=1;assert(console_ui_begin());assert(d.vp.w==960&&d.vp.h==544&&!d.ds);console_ui_sprite(UI_VITA,220,148,520,236);Texture font={.w=512,.h=512};float pos[4]={0,0,10,20},uv[4]={0,0,24,32};for(unsigned i=0;i<65;i++)console_ui_glyph(&font,pos,uv,0xffffffff,0xffffffff);assert(glyph_count==1);console_ui_end();assert(!glyph_count);console_ui_finish_frame();assert(d.ui_display==native_target->lv[0]);Device restored=d;restored.ui_display=NULL;assert(!memcmp(&restored,&before,sizeof d));console_ui_release();assert(!live&&!g_mem_tex&&!native_target&&!memcmp(&d,&before,sizeof d));g_vita_bindings_open=0;
 fail=1;assert(!console_ui_begin());console_ui_release();assert(!live&&!g_mem_tex&&!memcmp(&d,&before,sizeof d));
 assert(draws==63);free(g_mem);puts("UI: device state identical after 20 cycles, allocations fully released, failed allocation leaves state/resources intact");return 0;}
'''
with tempfile.TemporaryDirectory() as td:
 p=Path(td);(p/'test.c').write_text(head+(root/'port/src/d3d8/console_ui.c').read_text(encoding='utf-8')+tail,encoding='utf-8')
 cc=shutil.which('gcc') or 'J:/tools/mingw64/bin/gcc.exe'
 inc=root.parent/'tools/sdl2/SDL2-2.32.10/x86_64-w64-mingw32/include'
 subprocess.run([cc,'-std=gnu11','-O2','-I'+str(root/'port/src/rt'),'-I'+str(root/'port/src'),'-I'+str(root/'port/src/d3d8'),'-I'+str(inc),'-I'+str(inc/'SDL2'),str(p/'test.c'),'-lm','-o',str(p/'test.exe')],check=True)
 subprocess.run([str(p/'test.exe'),str(root/'port/assets/controls/controls.tga')],check=True)
