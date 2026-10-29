"""An exhausted guest heap must not turn an options label into a write at zero."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'port/src/rt/options.c').read_text(encoding='utf-8')
function = source[source.index('static uint32_t camera_string('):source.index('extern void F_0050c440_orig')]
head = r'''
#include "rt.h"
#include <assert.h>
#include <stdlib.h>
#include <setjmp.h>
uint8_t *g_mem;
static jmp_buf failure;
static int fail=1;
uint32_t gheap_alloc(uint32_t n) { assert(n<128);return fail?0:128; }
void rt_fatal(const char *format,...) { assert(strstr(format,"options text"));longjmp(failure,1); }
'''
tail = r'''
int main(void) {
 g_mem=malloc(512);assert(g_mem);memset(g_mem,0xa5,512);
 if(!setjmp(failure)) { camera_string(1,0);assert(!"expected an explicit failure"); }
 for(unsigned i=0;i<512;i++)assert(g_mem[i]==0xa5);
 fail=0;uint32_t p=camera_string(1,0);assert(p==128);uint8_t *mb=g_mem;
 assert(R16(p)==0x8000 && R16(p+2+strlen("Camera sensitivity"))==0xffff);
 for(unsigned i=0;i<128;i++)assert(g_mem[i]==0xa5);
 assert(camera_string(1,0)==128);free(g_mem);
 puts("options text: failed allocation leaves guest memory untouched and no invalid cache entry; retry returns valid encoded text");
}
'''
compiler = os.environ.get('CC') or shutil.which('gcc') or 'J:/tools/mingw64/bin/gcc.exe'
with tempfile.TemporaryDirectory(prefix='sh4-options-oom-') as tmp:
    path = Path(tmp)
    (path / 'test.c').write_text(head + function + tail, encoding='utf-8')
    subprocess.run([compiler, '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-I' + str(root / 'port/src/rt'), str(path / 'test.c'),
                    '-lm', '-o', str(path / 'test.exe')], check=True)
    subprocess.run([str(path / 'test.exe')], check=True)
