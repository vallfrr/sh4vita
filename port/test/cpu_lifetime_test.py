"""Production CPU allocation/release: ownership, rollback and repeated reuse."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root/'port/src/rt/x86rt.c').read_text(encoding='utf-8')
start = source.index('static CPU g_main_cpu_storage;')
end = source.index('\n#if defined(SH4_TRACE_CALLS)', start)
head = r'''
#include "rt.h"
#include <assert.h>
#include <stdlib.h>
#include <setjmp.h>
static uint8_t memory[64u<<20];uint8_t *g_mem=memory;uint32_t g_mem_size=sizeof memory;
static struct {uint32_t ptr,bytes;} slots[10];
static uint64_t guest_live;static unsigned host_live,host_calls,guest_calls,fail_host,fail_guest;
static jmp_buf fatal_target;static unsigned expect_fatal;
static void *owned_calloc(size_t n,size_t z){
 if(++host_calls==fail_host)return NULL;
 void *p=calloc(n,z);assert(p);host_live++;return p;
}
static void owned_free(void *p){if(p){assert(host_live);host_live--;free(p);}}
static uint32_t allocate(uint32_t n){
 if(++guest_calls==fail_guest)return 0;
 assert(n && n<=4u<<20);
 for(unsigned i=0;i<10;i++)if(!slots[i].ptr){
  uint32_t p=0x1000+i*(5u<<20);slots[i].ptr=p;slots[i].bytes=n;guest_live+=n;return p;
 }
 assert(0);return 0;
}
uint32_t gheap_calloc(uint32_t n){uint32_t p=allocate(n);if(p)memset(memory+p,0,n);return p;}
uint32_t gheap_alloc_aligned(uint32_t n,uint32_t align){assert(align==16);return allocate(n);}
void gheap_free(uint32_t p){
 for(unsigned i=0;i<10;i++)if(slots[i].ptr==p){guest_live-=slots[i].bytes;slots[i].ptr=0;return;}
 assert(!"unknown or double guest free");
}
void rt_log(const char *fmt,...){(void)fmt;}
_Noreturn void rt_fatal(const char *fmt,...){(void)fmt;assert(expect_fatal);longjmp(fatal_target,1);}
#define calloc owned_calloc
#define free owned_free
'''
tail = r'''
#undef calloc
#undef free
int main(void){
 CPU *main=rt_new_cpu(4u<<20);uint64_t base=guest_live;assert(!host_live);
 rt_free_cpu(NULL);rt_free_cpu(main);assert(guest_live==base);
 for(unsigned i=0;i<1000;i++){
  CPU *a=rt_new_cpu(1u<<20),*b=rt_new_cpu(0x40000);
  assert(a!=b && a!=main && b!=main && host_live==2);
  uint8_t *const mb=g_mem;
  assert(R32(a->fs_base+0x18)==a->fs_base && a->fcw==0x27f);
  assert(a->esp==R32(a->fs_base+4)-16 && R32(a->fs_base+8)<a->esp);
  /* Ownership must not be reconstructed from guest-mutable TIB fields. */
  W32(a->fs_base+4,0xdeadbeef);W32(a->fs_base+8,0xcafebabe);
  rt_free_cpu(a);assert(host_live==1);b->eax=i;assert(b->eax==i);
  rt_free_cpu(b);assert(guest_live==base && !host_live);
 }
 for(unsigned fault=0;fault<4;fault++){
  host_calls=guest_calls=0;fail_host=fault==0?1:0;
  fail_guest=fault==1?1:fault==2?2:0;expect_fatal=1;
  if(!setjmp(fatal_target)){rt_new_cpu(fault==3?0:1u<<20);assert(!"missing allocation fatal");}
  expect_fatal=0;assert(guest_live==base && !host_live);
 }
 puts("2000 worker CPU lifetimes: stacks/TIB released exactly once, main/NULL preserved, mutable TIB ownership safe; host/TIB/stack/invalid-size rollback passes");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    folder = Path(tmp)
    src = folder/'test.c'
    src.write_text(head+source[start:end]+tail, encoding='utf-8')
    exe = folder/'test.exe'
    subprocess.run([str(root.parent/'tools/mingw64/bin/gcc.exe'), '-std=c11', '-O2',
                    '-I'+str(root/'port/src/rt'), str(src), '-o', str(exe), '-lm'], check=True)
    subprocess.run([str(exe)], check=True)
