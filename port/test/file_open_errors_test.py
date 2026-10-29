"""Host allocation failure must not truncate a save/config or create a bad handle."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'port/src/win32/kernel32.c').read_text(encoding='utf-8')
function = source[source.index('K(CreateFileA) {'):source.index('static File *file_of')]
error = source[source.index('static uint32_t file_error(int error, uint32_t fallback) {'):source.index('static uint32_t file_status(')]
head = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
typedef struct { uint32_t args[8],result,error; } CPU;
typedef struct { FILE *f;char path[600];int ro,pfreuse; } File;
static File *opened;
static unsigned handles,opens;
static int allocation_error,open_error;
static char guest_path[600];
#define H_FILE 1
#define K(n) void imp_##n(CPU *c)
#define HOSTFN (void)0
#define ARG(n) c->args[n]
#define GSTR(a) guest_path
#define STDRET(n,v) do{c->result=(v);return;}while(0)
#define SDL_strcasecmp _stricmp
static void w32_host_path(const char *src,char *dst,size_t n) { snprintf(dst,n,"%s",src); }
static void w32_set_last_error(CPU *c,uint32_t e) { c->error=e; }
static void rt_log(const char *format,...) { (void)format;errno=EINVAL; }
static unsigned h_new(int kind,File *f) { assert(kind==H_FILE && f && !opened);handles++;opened=f;return 4; }
static void *test_calloc(size_t count,size_t n) { if(allocation_error) return NULL;return calloc(count,n); }
static FILE *test_open(const char *path,const char *mode) {
 opens++;if(open_error) { errno=open_error;return NULL; }return fopen(path,mode);
}
#define calloc test_calloc
#define fopen test_open
'''
tail = r'''
#undef calloc
#undef fopen
static void cleanup(void) { assert(opened);fclose(opened->f);free(opened);opened=NULL; }
static void existing(void) { FILE *f=fopen("probe.sav","wb");assert(f);fputs("previous-save",f);fclose(f); }
int main(void) {
 CPU c={0};strcpy(guest_path,"probe.sav");c.args[2]=0x40000000;c.args[5]=2;
 existing();allocation_error=1;imp_CreateFileA(&c);
 assert(c.result==0xffffffffu && c.error==8 && !handles && !opens && !opened);
 FILE *f=fopen("probe.sav","rb");char data[32]={0};assert(f);assert(fread(data,1,32,f)==13);fclose(f);
 assert(!strcmp(data,"previous-save"));
 allocation_error=0;open_error=EACCES;imp_CreateFileA(&c);
 assert(c.result==0xffffffffu && c.error==5 && !handles && opens==1 && !opened);
 c.args[5]=4;opens=0;imp_CreateFileA(&c);assert(c.error==5 && opens==1); // no create retry on permission errors
 open_error=ENOSPC;imp_CreateFileA(&c);assert(c.error==112 && !opened);
 open_error=0;c.args[2]=0x80000000;c.args[5]=3;imp_CreateFileA(&c);
 assert(c.result==4 && !c.error && opened->ro && !opened->pfreuse);cleanup();
 strcpy(guest_path,"missing.pac");c.args[2]=0x40000000;c.args[5]=4;opens=0;imp_CreateFileA(&c);
 assert(c.result==4 && !c.error && opens==2 && !opened->ro && !opened->pfreuse);cleanup();
 strcpy(guest_path,"probe.sav");c.args[2]=0x40000000;c.args[5]=2;imp_CreateFileA(&c);
 assert(c.result==4 && !c.error && !opened->ro && ftell(opened->f)==0);cleanup();
 puts("CreateFileA: OOM precedes any truncation/open, clean errors/no invalid handles, OPEN_ALWAYS retry only for absent file, success properties preserved");
}
'''
compiler = os.environ.get('CC') or shutil.which('gcc') or 'J:/tools/mingw64/bin/gcc.exe'
with tempfile.TemporaryDirectory(prefix='sh4-file-open-') as tmp:
    path = Path(tmp)
    (path / 'test.c').write_text(head + error + function + tail, encoding='utf-8')
    subprocess.run([compiler, '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
                    str(path / 'test.c'), '-o', str(path / 'test.exe')], check=True)
    subprocess.run([str(path / 'test.exe')], cwd=path, check=True)
