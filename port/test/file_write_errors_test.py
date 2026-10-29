"""Fault-inject the production Win32 file boundary; never opens a game save."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'port/src/win32/kernel32.c').read_text(encoding='utf-8')
write = source[source.index('static uint32_t file_error(int error, uint32_t fallback) {'):source.index('K(SetFilePointer)')]
close = source[source.index('K(FlushFileBuffers)'):source.index('K(CreateDirectoryA)')]
head = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
typedef struct { uint32_t args[8], result, error; } CPU;
typedef struct { FILE *f; int ro; unsigned pfcap; void *pf; } File;
static uint8_t memory[1024];
static const uint32_t g_mem_size=sizeof memory;
static File *opened;
static int kind, closed, stopped, event_count;
static unsigned s_pf_mem;
static int write_fault, flush_fault, close_fault, seek_fault, io_calls;
#define H_FREE 0
#define H_FILE 1
#define H_EVENT 2
#define K(name) void imp_##name(CPU *c)
#define HOSTFN uint8_t *mb=memory; (void)mb
#define ARG(n) c->args[n]
#define R32(p) (*(uint32_t *)(memory+(p)))
#define W32(p,v) (R32(p)=(v))
#define STDRET(n,v) do { c->result=(v);return; } while(0)
static File *file_of(uint32_t h) { return h==4 && kind==H_FILE ? opened : NULL; }
static int h_type(uint32_t h) { return h==4 ? kind : H_FREE; }
static void h_close(uint32_t h) { assert(h==4); kind=H_FREE; opened=NULL; closed++; }
static void pf_stop(File *f) { assert(f==opened); stopped++; }
static void pf_release(uint32_t n) {assert(s_pf_mem>=n);s_pf_mem-=n;}
static void pf_head_forget(File *f) {(void)f;}
static void w32_set_last_error(CPU *c, uint32_t e) { c->error=e; }
static void rt_log(const char *fmt, ...) { (void)fmt; errno=EINVAL; }
static void ev_set(uint32_t h, int v) { assert(h==44 && v==1); event_count++; errno=EINVAL; }
static size_t test_write(const void *p, size_t s, size_t n, FILE *f) {
    io_calls++;
    if(write_fault) { errno=write_fault==1?ENOSPC:0; return fwrite(p,s,n/2,f); }
    return fwrite(p,s,n,f);
}
static int test_flush(FILE *f) {
    io_calls++;
    if(flush_fault) { errno=ENOSPC;return EOF; }
    return fflush(f);
}
static int test_close(FILE *f) {
    io_calls++;
    int r=fclose(f);
    if(close_fault) { errno=ENOSPC;return EOF; }
    return r;
}
static int test_seek(FILE *f, long p, int w) {
    io_calls++;
    if(seek_fault) { errno=EIO;return -1; }
    return fseek(f,p,w);
}
#define fwrite test_write
#define fflush test_flush
#define fclose test_close
#define fseek test_seek
'''
tail = r'''
#undef fwrite
#undef fflush
#undef fclose
#undef fseek
static CPU fresh(void) {
    memset(memory,0,sizeof memory);
    for(unsigned i=0;i<32;i++) memory[128+i]=(uint8_t)(i+1);
    CPU c={0};c.args[1]=4;c.args[2]=128;c.args[3]=32;c.args[4]=64;c.error=777;
    write_fault=flush_fault=close_fault=seek_fault=io_calls=event_count=0;
    return c;
}
static void open_temp(void) {
    opened=calloc(1,sizeof *opened);assert(opened);
    opened->f=tmpfile();assert(opened->f);kind=H_FILE;
    opened->pf=malloc(64);assert(opened->pf);opened->pfcap=64;s_pf_mem=64;
}
static void finish(CPU *c) {
    c->args[1]=4;imp_CloseHandle(c);
    assert(c->result && !opened && kind==H_FREE && s_pf_mem==0);
}
int main(void) {
    CPU c=fresh();open_temp();imp_WriteFile(&c);
    assert(c.result && R32(64)==32 && c.error==777);
    imp_FlushFileBuffers(&c);assert(c.result);
    rewind(opened->f);uint8_t data[64]={0};assert(fread(data,1,64,opened->f)==32);
    assert(!memcmp(data,memory+128,32));finish(&c);

    c=fresh();R32(64)=99;imp_WriteFile(&c);
    assert(!c.result && c.error==6 && R32(64)==0 && !io_calls);
    imp_FlushFileBuffers(&c);assert(!c.result && c.error==6 && !io_calls);
    imp_CloseHandle(&c);assert(!c.result && c.error==6 && !io_calls);

    c=fresh();open_temp();opened->ro=1;imp_WriteFile(&c);
    assert(!c.result && c.error==5 && !R32(64) && !io_calls);
    imp_FlushFileBuffers(&c);assert(!c.result && c.error==5 && !io_calls);finish(&c);

    c=fresh();open_temp();write_fault=1;imp_WriteFile(&c);
    assert(!c.result && c.error==112 && R32(64)==16);finish(&c);
    c=fresh();open_temp();write_fault=2;imp_WriteFile(&c);
    assert(!c.result && c.error==29 && R32(64)==16);finish(&c);
    c=fresh();open_temp();flush_fault=1;imp_FlushFileBuffers(&c);
    assert(!c.result && c.error==112);finish(&c);
    c=fresh();open_temp();close_fault=1;imp_CloseHandle(&c);
    assert(!c.result && c.error==112 && !opened && kind==H_FREE && !s_pf_mem);
    imp_CloseHandle(&c);assert(!c.result && c.error==6);

    c=fresh();open_temp();c.args[3]=0;imp_WriteFile(&c);
    assert(c.result && !R32(64) && !io_calls);finish(&c);
    c=fresh();open_temp();c.args[2]=1010;imp_WriteFile(&c);
    assert(!c.result && c.error==998 && !R32(64) && !io_calls);finish(&c);

    c=fresh();open_temp();c.args[5]=256;R32(264)=5;R32(272)=44;
    imp_WriteFile(&c);assert(c.result && !R32(256) && R32(260)==32 && event_count==1);
    rewind(opened->f);memset(data,0xff,sizeof data);assert(fread(data,1,64,opened->f)==37);
    assert(!memcmp(data+5,memory+128,32));
    c.args[2]=256;c.args[3]=80;imp_GetOverlappedResult(&c);
    assert(c.result && R32(80)==32);finish(&c);

    c=fresh();open_temp();c.args[5]=256;R32(272)=44;write_fault=1;
    imp_WriteFile(&c);assert(!c.result && c.error==112 && R32(256)==0xc000007fu && R32(260)==16 && event_count==1);
    c.args[2]=256;c.args[3]=80;imp_GetOverlappedResult(&c);
    assert(!c.result && c.error==112 && R32(80)==16);finish(&c);

    c=fresh();open_temp();c.args[5]=256;seek_fault=1;imp_WriteFile(&c);
    assert(!c.result && c.error==25 && !R32(64) && io_calls==1);finish(&c);
    c=fresh();open_temp();c.args[5]=256;R32(268)=1;imp_WriteFile(&c);
    assert(!c.result && c.error==87 && !R32(64) && !io_calls);finish(&c);
    c=fresh();open_temp();imp_WriteFile(&c);
    c.args[5]=256;R32(264)=R32(268)=0xffffffffu;imp_WriteFile(&c);
    assert(c.result && ftell(opened->f)==64);finish(&c);

    c=fresh();kind=H_EVENT;imp_CloseHandle(&c);assert(c.result && kind==H_FREE);
    assert(closed==stopped+1);
    puts("file boundary: real temporary-file bytes/offsets and injected write/flush/close/seek failures passed");
}
'''
compiler = os.environ.get('CC') or shutil.which('gcc') or 'J:/tools/mingw64/bin/gcc.exe'
with tempfile.TemporaryDirectory(prefix='sh4-file-errors-') as tmp:
    path = Path(tmp)
    (path / 'test.c').write_text(head + write + close + tail, encoding='utf-8')
    subprocess.run([compiler, '-std=c11', '-Wall', '-Wextra', '-Werror', '-O2',
                    str(path / 'test.c'), '-o', str(path / 'test.exe')], check=True)
    subprocess.run([str(path / 'test.exe')], check=True)
