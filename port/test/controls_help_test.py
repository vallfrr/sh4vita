"""Exercise the actual fixed-page wrappers, text cache and font transform ABI."""
from pathlib import Path
import subprocess,tempfile,shutil
root=Path(__file__).resolve().parents[2]
code=r'''
#define SDL_MAIN_HANDLED
#include "rt.h"
#include "d3d8/console_ui.h"
#include <assert.h>
#include <stdlib.h>
#include <stdarg.h>
Device*g_dev;unsigned g_frame=7;uint8_t*g_mem;static unsigned alloc=0x20000,original,release_count,back,quad_calls;
static unsigned badge_calls,badge_color;
static int finished;static float expected[4];
uint32_t gheap_alloc(uint32_t n){unsigned p=alloc;alloc+=n;return p;}
void rt_fatal(const char*s,...){(void)s;abort();}
void rt_log(const char*s,...){(void)s;}
uint32_t rt_host_addr(gfn f,const char*s){assert(f&&s);return G_HOSTFN_BASE;}
uint32_t rt_guest_call(CPU*c,uint32_t fn,int n,...){
 (void)c;va_list ap;va_start(ap,n);uint32_t a[4]={0};for(int i=0;i<n;i++)a[i]=va_arg(ap,uint32_t);va_end(ap);
 if(fn==0x4f84a0){assert(n==2&&a[0]==0&&a[1]==1);return back;}
 assert(fn!=0x406090 && fn!=0x406910);return 0;
}
int console_ui_begin(void){return 1;}
void console_ui_end(void){}
void console_ui_glyph(Texture*t,const float p[4],const float u[4],uint32_t a,uint32_t b){(void)t;(void)p;(void)u;(void)a;(void)b;}
void d3d_addref(void*p){(void)p;}
void d3d_release(ComObj*p){(void)p;}
void F_00526490_orig(CPU*c){c->esp+=4;}
void F_00559a10_orig(CPU*c){c->esp+=4;}
void console_ui_font_scale(float*x,float*y){*x=*y=1;}
void console_ui_release(void){release_count++;}
void console_ui_sprite(int i,float x,float y,float w,float h){assert(i>=0&&i<=UI_VITA);assert(x>=0&&y>=0&&w>0&&h>0);}
void console_ui_sprite_color(int i,float x,float y,float w,float h,uint32_t color){badge_calls++;badge_color=color;console_ui_sprite(i,x,y,w,h);}
void console_ui_line(float a,float b,float c,float d,uint32_t col){(void)a;(void)b;(void)c;(void)d;(void)col;}
void console_ui_fill(float a,float b,float c,float d,uint32_t col){assert(!a&&!b&&c==960&&d==544&&col==0xff000000);}
void F_00518830_orig(CPU*c){original++;c->eax=finished;c->esp+=4;}
void F_005176a0_orig(CPU*c){original++;c->esp+=4;}
void F_00517e60_orig(CPU*c){original++;c->esp+=4;}
void F_00517c30_orig(CPU*c){original++;c->esp+=4;}
void F_0050b5b0_orig(CPU*c){original++;c->esp+=4;}
void F_00559e70_orig(CPU*c){original++;c->esp+=4;}
void F_00563d20_orig(CPU*c){uint8_t*mb=g_mem;quad_calls++;for(unsigned i=0;i<4;i++)assert(rt_f32(ARG(i+1))==expected[i]);assert(ARG(5)==0x12345678);c->esp+=4;}
'''
tail=r'''
int main(void){g_mem=calloc(1,32u<<20);assert(g_mem);uint8_t*mb=g_mem;CPU c={.esp=0x1000};
 W32(0x654d9c,99);rt_controls_patch_labels();assert(R32(0x654d9c)==99);
 putenv("SH4_CONSOLE_BINDINGS=1");rt_controls_patch_labels();assert(R32(0x654d9c)==0x7002);
 for(unsigned l=1;l<=5;l++)for(unsigned k=0;k<TEXT_COUNT;k++){unsigned p=string(l,k);assert(R16(p)==0x8000&&string(l,k)==p);}
 for(unsigned fade=0;fade<=128;fade+=8){W32(0x137f98c,fade);assert(menu_fade()==(fade==128?255:fade*2));}W32(0x137f98c,0);
 unsigned end=alloc;for(unsigned l=1;l<=5;l++)for(unsigned k=0;k<TEXT_COUNT;k++)string(l,k);assert(alloc==end);
 nat_vita_bindings(&c);assert(g_vita_bindings_open&&c.esp==0x1004);
 unsigned char state[0x294];memcpy(state,mb+0x108f4d8,sizeof state);
 unsigned before=original;c.esp=0x1000;back=0;nat_vita_bindings_input(&c);assert(!c.eax&&c.esp==0x1004&&before==original);
 c.esp=0x1000;back=1;nat_vita_bindings_input(&c);assert(c.eax==1&&c.esp==0x1004&&before==original);
 assert(!memcmp(state,mb+0x108f4d8,sizeof state));
 unsigned char font[0x44];memset(font,0x49,sizeof font);memcpy(mb+0x1261c00,font,sizeof font);
 c.esp=0x1000;nat_vita_bindings_draw(&c);assert(c.esp==0x1004&&!memcmp(font,mb+0x1261c00,0x28));
 uint32_t q[]={rt_u32f(10),rt_u32f(20),rt_u32f(30),rt_u32f(40),0x12345678,0,0,0,0x80808080,0x80808080};memcpy(mb+0x1004,q,sizeof q);
 for(unsigned mode=0;mode<2;mode++){placements[0]=(GlyphPlacement){10,20,.5f,.8f,g_frame,mode};for(unsigned i=0;i<4;i++)expected[i]=rt_f32(q[i]);c.esp=0x1000;c.esi=0x125724c;W32(c.esp,0x559b69);nat_controls_font_quad(&c);assert(c.esp==0x1004&&!memcmp(mb+0x1004,q,sizeof q));}
 assert(quad_calls==2 && glyph_n==1 && glyphs[0].top==0xffffffff && glyphs[0].bottom==0xffffffff);assert(glyph_color(0xb0000000)==0xff000000);finished=1;c.esp=0x1000;nat_vita_bindings(&c);assert(!g_vita_bindings_open);
 W32(0x137f98c,64);rt_controls_page_badges();assert(!badge_calls);c.esp=0x1000;nat_controls_options_page(&c);rt_controls_page_badges();assert(badge_calls==2&&badge_color==0x7fffffff);
 puts("controls: five-language cache, read-only cancel lifecycle, preserved font state, scoped geometry/UV/cdecl restoration passed");free(g_mem);}
'''
with tempfile.TemporaryDirectory() as td:
 p=Path(td);(p/'test.c').write_text(code+(root/'port/src/rt/controls_help.c').read_text(encoding='utf-8')+tail,encoding='utf-8')
 cc=shutil.which('gcc') or 'J:/tools/mingw64/bin/gcc.exe'
 subprocess.run([cc,'-std=gnu11','-O2','-Wall','-Wextra','-Werror','-I'+str(root/'port/src/rt'),'-I'+str(root/'port/src'),'-I'+str(root.parent/'tools/sdl2/SDL2-2.32.10/x86_64-w64-mingw32/include'),'-I'+str(root.parent/'tools/sdl2/SDL2-2.32.10/x86_64-w64-mingw32/include/SDL2'),str(p/'test.c'),'-o',str(p/'test.exe')],check=True)
 subprocess.run([str(p/'test.exe')],check=True)
