"""Inject allocation failures into the actual empty-level upload prefix."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[2];s=(r/'port/src/d3d8/resource.c').read_text()
prefix=s[s.index('void tex_upload(Texture *t, int l) {'):s.index('    int gl_l = l;')]
code=r'''
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
typedef struct{uint32_t w,h,mem,fmt;} Surface;
typedef struct{Surface *lv[1];} Texture;
static int fail_alloc,allocations,warnings,consumed;static uint8_t g_mem[128];
static void free_p(void *p){free(*(void**)p);}
static uint32_t fmt_level_size(uint32_t fmt,uint32_t w,uint32_t h){(void)fmt;return w*h*4;}
static void *test_calloc(size_t n,size_t sz){allocations++;return fail_alloc?NULL:calloc(n,sz);}
static void rt_log(const char *fmt,...){(void)fmt;warnings++;}
#define SLOW(a,b,c) ((void)0)
#define calloc test_calloc
'''+prefix+r'''
 assert(src);consumed++;
 if(smem)assert(src==g_mem+smem);
 else for(unsigned i=0;i<w*h*4;i++)assert(!src[i]);
}
int main(void){Surface v={4,4,0,0};Texture t={{&v}};
 fail_alloc=1;tex_upload(&t,0);assert(allocations==1&&warnings==1&&!consumed);
 fail_alloc=0;tex_upload(&t,0);assert(allocations==2&&warnings==1&&consumed==1);
 v.mem=32;fail_alloc=1;tex_upload(&t,0);assert(allocations==2&&warnings==1&&consumed==2);
 puts("Production empty-level upload: allocation failure stops safely; zero-fill and populated levels retain normal bytes");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(code);exe=p/'test.exe'
 subprocess.run([str(r.parent/'tools/mingw64/bin/gcc.exe'),'-std=c11','-O2','-Wall','-Wextra','-Werror',str(p/'test.c'),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
