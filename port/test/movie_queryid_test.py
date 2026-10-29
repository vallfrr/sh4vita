"""Exercise the real movie pin QueryId with and without guest heap space."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
src = (root / 'port/src/dx/movie.c').read_text(encoding='utf-8')
start = src.index('M(pin, QueryId) {')
func = src[start:src.index('\nM(pin, QueryPinInfo)', start)]
head = r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
typedef struct { uint32_t args[3], result; } CPU;
typedef struct { char id[20]; } HObj;
static HObj obj = { "Video" };
static uint8_t mem[512]; static int fail;
#define HOSTFN uint8_t *mb=mem; (void)mb
#define SELF HObj *self=&obj
#define M(a,b) void a##_##b(CPU *c)
#define ARG(n) c->args[n]
#define W32(p,v) (*(uint32_t *)(mem+(p))=(v))
#define W16(p,v) (*(uint16_t *)(mem+(p))=(v))
#define STDRET(n,v) do { c->result=(v);return; } while(0)
#define HR_OK 0
static uint32_t gheap_alloc(uint32_t size) { assert(size==12);return fail?0:128; }
'''
test = r'''
int main(void) {
 CPU c={0}; c.args[2]=64;memset(mem,0x5a,sizeof(mem));
 fail=1; pin_QueryId(&c);
 assert(c.result==0x8007000e && *(uint32_t *)(mem+64)==0);
 for(int i=0;i<12;i++) assert(mem[i]==0x5a);
 fail=0; pin_QueryId(&c);
 assert(c.result==0 && *(uint32_t *)(mem+64)==128);
 for(int i=0;i<6;i++) assert(*(uint16_t *)(mem+128+2*i)==(uint8_t)"Video"[i]);
 puts("movie pin QueryId: allocation failure is safe and success returns UTF-16");
}
'''
with tempfile.TemporaryDirectory(prefix='sh4-movie-queryid-') as tmp:
    c = Path(tmp) / 'test.c'
    exe = Path(tmp) / 'test.exe'
    c.write_text(head + func + test, encoding='utf-8')
    subprocess.run(['gcc', '-std=c11', '-O2', str(c), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
