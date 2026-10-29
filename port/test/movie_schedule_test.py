"""Movie scheduling must not reenter during the original player's Presents."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root/'port/src/d3d8/device.c').read_text(encoding='utf-8')
start = source.index('    static const char *pm;', source.index('M(Present)'))
block = source[start:source.index('    extern void rt_controls_present', start)]
head = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
typedef struct {int unused;} CPU;
static CPU cpu,*g_main_cpu;static unsigned g_frame,n,depth;static const char *setting;
#define getenv(name) setting
#define rt_log(...) ((void)0)
static void pump(void);
static void rt_guest_call(CPU *c,uint32_t fn,int argc,unsigned id){
 assert(c==&cpu&&fn==0x412ef0&&argc==1);assert(!depth);depth++;
 assert(id==7+n);n++;
 /* The nested movie Presents overrun the next scheduled absolute frames. */
 for(unsigned i=0;i<4;i++){g_frame+=100;pump();}
 depth--;
}
static void pump(void){
'''
tail = r'''
}
int main(int argc,char **argv){
 assert(argc==2);setting=argv[1];
 for(unsigned i=0;i<5;i++){g_frame++;pump();}assert(!n);
 g_main_cpu=&cpu;
 for(unsigned i=0;i<20;i++){g_frame++;pump();}
 unsigned expected=strstr(setting,"2:7,")?3:0;assert(n==expected&&!depth);
 puts("Movie schedule passed: no recursive playback, overdue entries run once in order, absent CPU/invalid IDs/zero frames safe");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    folder = Path(tmp)
    src = folder/'test.c'
    src.write_text(head+block+tail, encoding='utf-8')
    exe = folder/'test.exe'
    subprocess.run([str(root.parent/'tools/mingw64/bin/gcc.exe'), '-std=c11', '-O2',
                    str(src), '-o', str(exe)], check=True)
    for schedule in ('2:7,3:8,4:9', '', '2:14', '0:1', 'garbage'):
        subprocess.run([str(exe), schedule], check=True)
