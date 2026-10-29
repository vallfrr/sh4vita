"""Exercise the production prefetch job lifecycle without Vita or game data.

Compile the actual File/pf_start/policy code with controlled allocator and job
boundaries. Check retained bytes/jobs, short reads, replacement and failures.
"""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[2]


def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--cc',default='gcc')
    args=ap.parse_args()
    text=(ROOT/'port/src/win32/kernel32.c').read_text()
    file=re.search(r'typedef struct \{\n    FILE \*f; char path\[600\].*?\n\} File;',text,re.S).group()
    first=text.index('static void pf_start(');last=text.index('\n// true if',first)
    budget=text[text.index('static int pf_reserve('):text.index('// Bounded BGM head cache')]
    policy=text[text.index('    const char *reuse=getenv('):text.index('    w32_set_last_error(c, 0);',text.index('    const char *reuse=getenv('))]
    source=r'''
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#define PF_MAX 1310720u
typedef struct {int value;} SDL_atomic_t;
typedef int SDL_Thread;
static int SDL_AtomicGet(SDL_atomic_t *a) {return a->value;}
static void SDL_AtomicSet(SDL_atomic_t *a,int v) {a->value=v;}
static int SDL_strcasecmp(const char *a,const char *b) {
    while(*a && tolower((unsigned char)*a)==tolower((unsigned char)*b)) {++a;++b;}
    return tolower((unsigned char)*a)-tolower((unsigned char)*b);
}
'''+file+r'''
static unsigned s_pf_mem,stops,starts,allocs,fail_alloc;
static void pf_stop(File *f) {if(f->th) {++stops;f->th=NULL;SDL_AtomicSet(&f->pfabort,1);}}
static int pf_run(void *p) {(void)p;return 0;}
static SDL_Thread *SDL_CreateThread(int (*fn)(void *),const char *name,void *p) {
    (void)fn;(void)name;(void)p;++starts;return (SDL_Thread *)(uintptr_t)1;
}
static void *allocation(void *p,size_t n) {++allocs;return fail_alloc ? NULL : realloc(p,n);}
#define realloc allocation
'''+budget+text[first:last]+r'''
#undef realloc
static const char *option;
#define getenv(name) option
static int eligible(const char *hp,int ro,const char *value) {
    File actual={0},*fl=&actual;fl->ro=ro;option=value;
'''+policy+r'''
    return fl->pfreuse!=0;
}
#undef getenv
int main(void) {
    const char *paths[]={"ux0:data/sh4/demo/bgm.sh4","a.PAC","a.sav","x.sh4/save.json","x.pac.bak"};
    for(unsigned i=0;i<5;i++) for(int ro=0;ro<2;ro++) {
        assert(eligible(paths[i],ro,"1")==((i<2)&&ro));
        assert(!eligible(paths[i],ro,NULL));assert(!eligible(paths[i],ro,"0"));
    }
    unsigned kept=0,replaced=0;
    for(unsigned bits=0;bits<128;bits++) {
        File f={0};f.pfreuse=bits&1;f.pfcap=64;f.pf=malloc(64);memset(f.pf,0xa5,64);
        f.th=(bits&2) ? (SDL_Thread *)(uintptr_t)1 : NULL;
        f.pfpos=1234;f.pfwant=32;
        f.pfdone.value=(bits&4)!=0;f.pfgot.value=(bits&8) ? 32 : 16;
        long pos=(bits&16) ? 1235 : 1234;unsigned n=(bits&32) ? 33 : 32;
        if(bits&64) {n=PF_MAX+123;f.pfwant=PF_MAX;f.pfcap=PF_MAX;free(f.pf);f.pf=malloc(PF_MAX);memset(f.pf,0xa5,PF_MAX);f.pfgot.value=PF_MAX;}
        unsigned want=n>PF_MAX ? PF_MAX : n;
        int retain=f.pfreuse && f.th && pos==f.pfpos && want==f.pfwant &&
            (!f.pfdone.value || (unsigned)f.pfgot.value>=want);
        File before=f;starts=stops=allocs=fail_alloc=0;s_pf_mem=f.pfcap;
        pf_start(&f,pos,n);
        if(retain) {
            assert(!memcmp(&f,&before,sizeof f));assert(!starts && !stops && !allocs);++kept;
        } else {
            assert(stops==(before.th!=NULL) && starts==1);
            assert(f.pfpos==pos && f.pfwant==want && f.pfcap>=want);
            assert(!f.pfdone.value && !f.pfgot.value && !f.pfabort.value);++replaced;
        }
        // No read job runs in this adapter: metadata changes must not alter old bytes.
        for(unsigned k=0;k<before.pfcap;k++) assert(f.pf[k]==0xa5);
        free(f.pf);
    }
    for(unsigned fail=0;fail<2;fail++) {
        File f={0};f.pfcap=64;f.pf=malloc(64);memset(f.pf,0x7b,64);f.th=(SDL_Thread *)(uintptr_t)1;
        starts=stops=allocs=0;fail_alloc=fail;s_pf_mem=fail ? 64 : 2*PF_MAX;
        pf_start(&f,100,128);
        assert(stops==1 && starts==0 && f.pfcap==64);
        for(unsigned k=0;k<64;k++) assert(f.pf[k]==0x7b);
        free(f.pf);
    }
    printf("128 production lifecycle cases: %u retained jobs/buffers, %u replacements; archive policy, short-read retry, budget/allocation failures and retained bytes pass\n",kept,replaced);
}
'''
    with tempfile.TemporaryDirectory(prefix='sh4-prefetch-test-') as tmp:
        p=Path(tmp)/'test.c';p.write_text(source)
        exe=Path(tmp)/'test.exe'
        subprocess.run([args.cc,'-std=c11','-O2','-Wall','-Werror',str(p),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)


if __name__=='__main__':main()
