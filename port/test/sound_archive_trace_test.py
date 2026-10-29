"""The resident-bank observer must leave guest memory and CPU untouched."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
s=(root/'port/src/rt/natives.c').read_text();a=s.index('static void sound_archive_trace(');b=s.index('// 0x551f80:',a);fn=s[a:b]
head=r'''
#include "rt.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
uint8_t *g_mem;uint32_t g_mem_size=1u<<20;unsigned g_frame=100;static unsigned logged;
void rt_log(const char *fmt,...) {(void)fmt;logged++;}
void F_00409530_orig(CPU *c) {c->eax=1;}
void F_00409a30_orig(CPU *c) {c->eax=2;}
void rt_sound_archive_verify(CPU *c,uint32_t obj,uint32_t id) {(void)c;(void)obj;(void)id;}
'''
test=r'''
int main(void) {
 g_mem=calloc(1,g_mem_size);uint8_t *mb=g_mem;assert(mb);CPU c={0},copy;
 c.ecx=0x2000;c.esp=0x4000;W32(c.esp+4,10);W32(0x30ec,0x10000);
 copy=c;sound_archive_trace(&c,0x409530);assert(logged==1&&!memcmp(&c,&copy,sizeof(c)));
 W32(c.esp+4,355);sound_archive_trace(&c,0x409530);assert(logged==1);
 c.esp=g_mem_size-4;sound_archive_trace(&c,0x409530);assert(logged==1);
 c.esp=0x4000;c.ecx=g_mem_size-4;sound_archive_trace(&c,0x409530);assert(logged==1);
 c.ecx=0x2000;W32(c.esp+4,10);nat_sound_request(&c);assert(c.eax==1);
 nat_sound_request3d(&c);assert(c.eax==2);free(g_mem);
 puts("sound archive observer preserves CPU and guards guest spans");
}
'''
with tempfile.TemporaryDirectory(prefix='sh4-sound-trace-') as d:
 c=Path(d)/'test.c';exe=Path(d)/'test.exe';c.write_text(head+fn+test)
 subprocess.run(['gcc','-std=c11','-O2','-I'+str(root/'port/src/rt'),str(c),'-o',str(exe)],check=True)
 import os
 subprocess.run([str(exe)],env={**os.environ,'SH4_SOUND_ARCHIVE_TRACE':'1'},check=True)
