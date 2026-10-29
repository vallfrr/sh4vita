"""Distinguish EOF padding from real stdio/direct descriptor failures."""
from pathlib import Path
import os,shutil,subprocess,tempfile
root=Path(__file__).resolve().parents[2]
src=(root/'port/src/win32/kernel32.c').read_text(encoding='utf-8')
read=src[src.index('static uint32_t read_direct('):src.index('// Translate storage failures')]
helpers=src[src.index('static uint32_t file_error(int error, uint32_t fallback) {'):src.index('K(WriteFile)')]
head=r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
typedef struct { uint32_t args[8],result,error,esp; } CPU;
typedef struct { FILE *f; int ro, pfreuse; } File;
static uint8_t memory[65536];
static const uint32_t g_mem_size=sizeof memory;
static File opened;
static CPU *g_main_cpu;
static uint64_t g_t_io,g_io_bytes;
static unsigned g_frame;
static int read_fault,seek_fault,tell_fault,fake_error,prefetch_hit,planned,events,io_calls,descriptor_mode,descriptor_calls;
#define K(name) void imp_##name(CPU *c)
#define HOSTFN uint8_t *mb=memory; (void)mb
#define ARG(n) c->args[n]
#define R32(p) (*(uint32_t *)(memory+(p)))
#define W32(p,v) (R32(p)=(v))
#define STDRET(n,v) do {c->result=(v);return;} while(0)
#define PF_MIN 16384u
static File *file_of(uint32_t h) {return h==4?&opened:NULL;}
static int pf_head_take(File *f,long p,uint8_t *d,uint32_t n){(void)f;(void)p;(void)d;(void)n;return 0;}
static void pf_head_remember(File *f,long p,const uint8_t *d,uint32_t n){(void)f;(void)p;(void)d;(void)n;}
static void pf_head_pressure(File *f,uint32_t n){(void)f;(void)n;}
static void w32_set_last_error(CPU *c,uint32_t e){c->error=e;}
static void rt_log(const char *fmt,...){(void)fmt;errno=EINVAL;}
static void ev_set(uint32_t h,int v){assert(h==44 && v==1);++events;errno=EINVAL;}
static uint64_t SDL_GetPerformanceCounter(void){static uint64_t n;return n+=1000;}
static uint64_t SDL_GetPerformanceFrequency(void){return 1000000;}
#define SDL_memset memset
static int pf_take(File *f,long pos,uint8_t *dst,uint32_t n) {
 assert(f==&opened && pos>=0);if(!prefetch_hit)return 0;memset(dst,0x88,n);return 1;
}
static void pf_plan(File *f,long pos,uint32_t n){assert(f==&opened && pos>=0);(void)n;++planned;errno=EINVAL;}
static size_t test_read(void *p,size_t size,size_t count,FILE *f) {
 ++io_calls;if(read_fault){fake_error=1;errno=read_fault==1?EIO:0;return fread(p,size,count<4?count:4,f);}
 return fread(p,size,count,f);
}
static int test_error(FILE *f){return fake_error||ferror(f);}
static void test_clear(FILE *f){fake_error=0;clearerr(f);}
static int test_seek(FILE *f,long offset,int whence) {
 ++io_calls;if(seek_fault){errno=EACCES;return -1;}return fseek(f,offset,whence);
}
static long test_tell(FILE *f){if(tell_fault){errno=EIO;return -1;}return ftell(f);}
static int test_descriptor_read(int fd,void *dst,unsigned n) {
 (void)fd;++descriptor_calls;
 if(descriptor_mode==1 && descriptor_calls==1){errno=EINTR;return -1;}
 if(descriptor_mode==2 && descriptor_calls==2){errno=EIO;return -1;}
 if(descriptor_mode==3){errno=EIO;return -1;}
 if(descriptor_calls>(descriptor_mode==1?2:1))return 0;
 unsigned count=n<3?n:3;memset(dst,0x36,count);return (int)count;
}
#define fread test_read
#define ferror test_error
#define clearerr test_clear
#define fseek test_seek
#define ftell test_tell
#define read test_descriptor_read
'''
tail=r'''
#undef fread
#undef fseek
#undef ftell
static CPU start(unsigned size) {
 CPU c={0};c.args[1]=4;c.args[2]=1024;c.args[3]=size;c.args[4]=64;
 rewind(opened.f);read_fault=seek_fault=tell_fault=fake_error=prefetch_hit=planned=events=io_calls=descriptor_calls=descriptor_mode=0;
 memset(memory,0xaa,sizeof memory);g_main_cpu=NULL;return c;
}
int main(void) {
 opened.f=tmpfile();assert(opened.f);assert(fwrite("ABCDEFGH",1,8,opened.f)==8);fflush(opened.f);CPU c;
 c=start(16);imp_ReadFile(&c);assert(c.result==1 && R32(64)==8 && !memcmp(memory+1024,"ABCDEFGH",8));
 for(int i=8;i<16;i++)assert(memory[1024+i]==0);
 c=start(8);imp_ReadFile(&c);assert(c.result==1 && R32(64)==8 && planned==1);
 c=start(16);read_fault=1;imp_ReadFile(&c);assert(!c.result && c.error==1117 && R32(64)==4 && !planned);
 c=start(16);read_fault=2;imp_ReadFile(&c);assert(!c.result && c.error==1117 && R32(64)==4);
 c=start(8);c.args[1]=0;imp_ReadFile(&c);assert(!c.result && c.error==6 && !R32(64) && !io_calls);
 c=start(65536);imp_ReadFile(&c);assert(!c.result && c.error==998 && !R32(64) && !io_calls && memory[1024]==0xaa);
 c=start(8);c.args[5]=128;W32(136,0);W32(140,1);W32(144,44);imp_ReadFile(&c);
 assert(!c.result && c.error==87 && !R32(64) && R32(128)==0xc000000du && events==1);
 c=start(8);c.args[5]=128;W32(136,0);W32(140,0);W32(144,44);seek_fault=1;imp_ReadFile(&c);
 assert(!c.result && c.error==5 && !R32(64) && events==1);
 c=start(5);c.args[5]=128;W32(136,3);W32(140,0);W32(144,44);imp_ReadFile(&c);
 assert(c.result && R32(64)==5 && !memcmp(memory+1024,"DEFGH",5) && !R32(128) && R32(132)==5 && events==1);
 c=start(8);tell_fault=1;imp_ReadFile(&c);assert(!c.result && c.error==25 && !R32(64));
 c=start(16384);prefetch_hit=1;imp_ReadFile(&c);assert(c.result && R32(64)==16384 && memory[1024]==0x88 && planned==1);
 c=start(16384);prefetch_hit=1;seek_fault=1;imp_ReadFile(&c);assert(!c.result && c.error==5 && R32(64)==16384 && !planned);
 c=start(16384);
#ifdef __vita__
 seek_fault=1;imp_ReadFile(&c);assert(!c.result && c.error==5 && !R32(64));
 c=start(16384);
 descriptor_mode=2;imp_ReadFile(&c);assert(!c.result && c.error==1117 && R32(64)==3 && !planned);
 c=start(16384);descriptor_mode=1;imp_ReadFile(&c);assert(c.result && R32(64)==3 && descriptor_calls==3);
 c=start(16384);descriptor_mode=3;imp_ReadFile(&c);assert(!c.result && c.error==1117 && !R32(64));
#else
 read_fault=1;imp_ReadFile(&c);assert(!c.result && c.error==1117 && R32(64)==4);
 c=start(16384);imp_ReadFile(&c);assert(c.result && R32(64)==8 && !planned);
#endif
 fclose(opened.f);puts("ReadFile: EOF padding retained; invalid handle/bounds/offset/seek, partial errors, prefetch and OVERLAPPED completion passed");
}
'''
with tempfile.TemporaryDirectory(prefix='sh4-read-errors-') as tmp:
    p=Path(tmp);(p/'test.c').write_text(head+helpers+read+tail,encoding='utf-8')
    cc=os.environ.get('CC') or shutil.which('gcc') or 'J:/tools/mingw64/bin/gcc.exe'
    for mode in ('stdio','vita-direct'):
        subprocess.run([cc,'-std=gnu11','-O2',*(['-D__vita__'] if mode=='vita-direct' else []),str(p/'test.c'),'-o',str(p/'test.exe')],check=True)
        subprocess.run([str(p/'test.exe')],check=True)
        print(mode+' backend passed')
