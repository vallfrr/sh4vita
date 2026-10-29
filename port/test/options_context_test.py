from pathlib import Path
import shutil,subprocess,tempfile
s=Path('port/src/rt/natives.c').read_text();a=s.index('int g_options_active = 0;');b=s.index('\n}',a)+2
head=r'''
#include "rt.h"
#include <assert.h>
extern int g_options_active;
static int result,ends;
void rt_log(const char *s,...){(void)s;}
void rt_camera_end(void){ends++;}
void F_00526be0_orig(CPU*c){assert(g_options_active);c->eax=result;c->esp+=4;}
'''

tail=r'''
int main(void){assert(!g_options_active);for(int i=0;i<100;i++){CPU c={.esp=0x1000};result=0;nat_optframe(&c);assert(g_options_active && !c.eax && c.esp==0x1004 && !ends);}CPU c={.esp=0x1000};result=1;nat_optframe(&c);assert(!g_options_active && c.eax==1 && ends==1 && c.esp==0x1004);result=0;nat_optframe(&c);assert(g_options_active);result=2;nat_optframe(&c);assert(!g_options_active && ends==2);puts("options context: active before original update, maintained across frames, cleared immediately for both completion results, clean re-entry and ABI passed");}
'''
with tempfile.TemporaryDirectory() as td:
 p=Path(td);(p/'test.c').write_text(head+s[a:b]+tail)
 cc=shutil.which('gcc') or 'J:/tools/mingw64/bin/gcc.exe'
 subprocess.run([cc,'-std=gnu11','-O2','-Wall','-Wextra','-Werror','-Iport/src/rt',str(p/'test.c'),'-o',str(p/'test.exe')],check=True);subprocess.run([str(p/'test.exe')],check=True)
