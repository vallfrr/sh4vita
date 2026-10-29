"""Execute production COM creation with heap-failure injection."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
s=(root/'port/src/d3d8/com.c').read_text();s=s[s.index('#define MAX_OBJ'):s.index('\nComObj *com_get(')]
head=r'''
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <assert.h>
#include <stdio.h>
typedef void (*gfn)(void);
typedef struct { int kind,refs;uint32_t guest; } ComObj;
static uint8_t memory[4096];static jmp_buf fatal;static unsigned attempts,fail_at,next=128;
#define HOSTFN
#define W32(p,v) (*(uint32_t *)(memory+(p))=(v))
static uint32_t gheap_alloc(uint32_t bytes) {if(++attempts==fail_at)return 0;unsigned p=next;next+=bytes;return p;}
static uint32_t rt_host_addr(gfn fn,const char *name) {(void)fn;(void)name;return 0x1234;}
static void rt_fatal(const char *fmt,...) {(void)fmt;longjmp(fatal,1);}
'''
test=r'''
static void method(void) {}
int main(void) {
 gfn methods[2]={method,method};ComObj o={0};
 for(unsigned failure=1;failure<=2;failure++) {
  memset(memory,0x5a,sizeof(memory));memset(objs,0,sizeof(objs));nvtbl=0;attempts=0;fail_at=failure;
  if(!setjmp(fatal)){com_wrap(&o,7,methods,2,"test");assert(!"allocation failure must stop before invalid write");}
  for(unsigned j=0;j<16;j++)assert(memory[j]==0x5a);
  for(unsigned j=0;j<MAX_OBJ;j++)assert(!objs[j]);
 }
 fail_at=0;attempts=0;uint32_t address=com_wrap(&o,7,methods,2,"test");
 assert(address && o.refs==1 && o.kind==7 && *(uint32_t *)(memory+address+8)==0x44334438);
 assert(objs[*(uint32_t *)(memory+address+4)]==&o);
 unsigned before=attempts;ComObj other={0};com_wrap(&other,7,methods,2,"test");assert(attempts==before+1);
 puts("COM allocation failures stop before address-zero writes; success and vtable reuse preserved");
}
'''
with tempfile.TemporaryDirectory(prefix='sh4-com-') as d:
 code=Path(d)/'test.c';exe=Path(d)/'test.exe';code.write_text(head+s+test)
 subprocess.run(['gcc','-std=c11','-O2',str(code),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
