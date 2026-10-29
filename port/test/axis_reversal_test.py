from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
s=(root/'port/gen/x86_f002.c').read_text(encoding='utf-8')
def fn(s,n):
 a=s.index('void '+n+'(CPU *c) {'); return s[a:s.index('\n}',a)+2]
orig=fn(s,'F_00406f50_orig')
n=(root/'port/src/rt/natives.c').read_text(encoding='utf-8'); wrap=fn(n,'nat_axischange')
head='''#include "rt.h"\n#include <assert.h>\n#include <stdlib.h>\nuint8_t *g_mem; static unsigned mask;\nvoid F_00406c50(CPU*c){uint8_t*mb=g_mem; unsigned code=R32(c->esp+4)&255, release=R32(c->esp+8); if(code==1||code==3){if(release)mask&=~(1u<<code);else mask|=1u<<code;} c->esp+=12;}\n'''
test='''\nstatic void step(int v,int wrapped){ uint8_t*mb=g_mem;CPU c={0};c.esp=0x1000;c.ecx=0x3000;W32(0x1004,(uint32_t)v);W32(0x1008,0x2000);W32(0x100c,0);if(wrapped)nat_axischange(&c);else F_00406f50_orig(&c);assert(c.esp==0x1010); }\nint main(void){g_mem=calloc(1,65536);assert(g_mem);step(700,0);assert(mask==(1u<<3));step(-700,0);assert(mask==((1u<<3)|(1u<<1)));step(0,0);assert(mask==0);step(700,1);assert(mask==(1u<<3));step(-700,1);assert(mask==(1u<<1));step(700,1);assert(mask==(1u<<3));step(0,1);assert(mask==0);puts("axis reversal releases the previous direction; stock handler retains it");return 0;}\n'''
with tempfile.TemporaryDirectory(prefix='sh4-axis-') as d:
 c=Path(d)/'test.c'; exe=Path(d)/'test.exe'; c.write_text(head+orig+'\n'+wrap+test)
 subprocess.run(['gcc','-std=gnu11','-O2','-I'+str(root/'port/src/rt'),str(c),'-lm','-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
