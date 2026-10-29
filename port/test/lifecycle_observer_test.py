"""Test the observer contract without claiming a real Vita suspend/resume."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'port/src/vita/lifecycle.c').read_text(encoding='utf-8')
source = source[source.index('typedef struct {'):]
head = r'''
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdarg.h>
typedef int SceUID;
#define SCE_POWER_CB_SYSTEM_RESUME 0x40000
#define SCE_POWER_CB_APP_RESUME 0x200000

unsigned g_frame;
static int guard_enabled,neutralized;
static int enabled,create_error,register_error,created,registered,deleted,checked,queued,in_callback,lines,drop_lines;
static int (*callback_function)(int,int,int,void*);
static char *test_env(const char *name) { if(!strcmp(name,"SH4_RESUME_GUARD"))return guard_enabled?"1":NULL;assert(!strcmp(name,"SH4_LIFECYCLE_TRACE"));return enabled?"1":NULL; }
static SceUID sceKernelCreateCallback(const char *name,unsigned attr,int (*f)(int,int,int,void*),void *arg) {
 assert(!in_callback && name && !attr && !arg);created++;callback_function=f;return create_error?-1:4;
}
static int scePowerRegisterCallback(int cb) { assert(!in_callback && cb==4);registered++;return register_error?-2:0; }
static int sceKernelDeleteCallback(int cb) { assert(!in_callback && cb==4);deleted++;return 0; }
static uint64_t sceKernelGetProcessTimeWide(void) { assert(in_callback);return 1000; }
static uint64_t sceKernelGetSystemTimeWide(void) { assert(in_callback);return 2000; }
static int sceKernelCheckCallback(void) {
 checked++;
 for(int i=0;i<queued;i++) { in_callback=1;assert(!callback_function(1,1,0x200000+i,NULL));in_callback=0; }
 queued=0;return 0;
}
static void rt_log(const char *format,...) {
 assert(!in_callback);
 if(strstr(format,"flags="))lines++;
 if(strstr(format,"dropped="))drop_lines++;
}
#define getenv test_env
static void rt_input_neutralize(void) { assert(!in_callback);neutralized++; }
'''
tail = r'''
#undef getenv
static void reset(void) {
 initialized=0;callback=-1;produced=consumed=dropped=0;resume_guard=guard_enabled=neutralized=0;
 enabled=create_error=register_error=created=registered=deleted=checked=queued=in_callback=lines=drop_lines=0;
}
int main(void) {
 reset();for(int i=0;i<100;i++)rt_vita_lifecycle_poll();assert(!created && !registered && !checked);
 reset();enabled=1;create_error=1;rt_vita_lifecycle_poll();assert(created==1 && !registered && !deleted && !checked);
 reset();enabled=1;register_error=1;rt_vita_lifecycle_poll();assert(created==1 && registered==1 && deleted==1 && !checked);
 reset();enabled=1;g_frame=100;queued=10;rt_vita_lifecycle_poll();
 assert(created==1 && registered==1 && checked==1 && lines==8 && drop_lines==1);
 assert(consumed==produced && !dropped);
 for(int i=0;i<100;i++) { rt_vita_lifecycle_poll(); }
 assert(checked==1 && lines==8);
 g_frame=101;queued=2;rt_vita_lifecycle_poll();assert(checked==2 && lines==10 && drop_lines==1);
 assert(created==1 && registered==1 && !deleted);
 reset();guard_enabled=1;g_frame=900;queued=1;rt_vita_lifecycle_poll();assert(created==1 && neutralized==1);
 in_callback=1;callback_function(1,1,0x1000,NULL);in_callback=0;g_frame++;rt_vita_lifecycle_poll();assert(neutralized==1);
 in_callback=1;callback_function(1,1,SCE_POWER_CB_SYSTEM_RESUME,NULL);in_callback=0;g_frame++;rt_vita_lifecycle_poll();assert(neutralized==2);
 puts("lifecycle observer: disabled mode has no kernel calls; registration failure cleans up; callbacks only enqueue; bounded overflow and once-per-frame dispatch passed");
}
'''
compiler = os.environ.get('CC') or shutil.which('gcc') or 'J:/tools/mingw64/bin/gcc.exe'
with tempfile.TemporaryDirectory(prefix='sh4-lifecycle-') as tmp:
    path = Path(tmp)
    (path / 'test.c').write_text(head + source + tail, encoding='utf-8')
    subprocess.run([compiler, '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
                    str(path / 'test.c'), '-o', str(path / 'test.exe')], check=True)
    subprocess.run([str(path / 'test.exe')], check=True)
