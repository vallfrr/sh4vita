"""Diagnostic menu selection must not bypass disabled Continue/Load entries."""
from pathlib import Path
import os,shutil,subprocess,tempfile
root=Path(__file__).resolve().parents[2]
test=r'''
#include "rt.h"
#include <assert.h>
#include <stdlib.h>
unsigned g_frame;
static uint32_t response=0xffffffffu;
static unsigned calls;
void rt_log(const char *fmt,...) { (void)fmt; }
void F_00528a50_orig(CPU *c) { ++calls;c->eax=response;c->esp+=4; }
void nat_titlechoice(CPU *c);
int main(int argc,char **argv) {
 assert(argc==3);_putenv_s("SH4_TITLE_SELECT",argv[1]);int expected=atoi(argv[2]);
 CPU c={.esp=100};g_frame=1000;nat_titlechoice(&c);assert(c.eax==0xffffffffu && c.esp==104);
 response=1;g_frame=1500;nat_titlechoice(&c);assert(c.eax==1);
 response=0xffffffffu;g_frame=1501;nat_titlechoice(&c);
 assert(c.eax==(expected<0?0xffffffffu:(uint32_t)expected));
 nat_titlechoice(&c);assert(c.eax==0xffffffffu && calls==4 && c.esp==116);
}
'''
with tempfile.TemporaryDirectory(prefix='sh4-title-dest-') as tmp:
    p=Path(tmp);(p/'test.c').write_text(test,encoding='utf-8')
    cc=os.environ.get('CC') or shutil.which('gcc') or 'J:/tools/mingw64/bin/gcc.exe'
    subprocess.run([cc,'-std=gnu11','-O2','-I'+str(root/'port/src/rt'),str(p/'test.c'),str(root/'port/src/rt/menu_test.c'),'-lm','-o',str(p/'test.exe')],check=True)
    for value,expected in [('1200:0',0),('1200:3',3),('1200:1',-1),('1200:2',-1),('1200:4',-1),('1200:3oops',-1),('',-1)]:
        subprocess.run([str(p/'test.exe'),value,str(expected)],check=True)
print('diagnostic menu destination: only New Game/Options; real input prioritized, timing/once/ABI and disabled destinations passed')
