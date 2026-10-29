from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[2];s=(r/'port/src/win32/kernel32.c').read_text();budget=s[s.index('static int pf_reserve('):s.index('// Bounded BGM head cache')]
code=r'''
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#define PF_MAX 1310720u
static uint32_t s_pf_mem;
'''+budget+r'''
static DWORD WINAPI worker(LPVOID arg){
 unsigned id=(unsigned)(uintptr_t)arg;
 for(unsigned i=0;i<10000;i++){
  unsigned n=262144u+((i+id)%4)*131072u;
  if(pf_reserve(n)){assert(__atomic_load_n(&s_pf_mem,__ATOMIC_RELAXED)<=2*PF_MAX);SwitchToThread();pf_release(n);}
  else SwitchToThread();
 }
 return 0;
}
int main(void){
 HANDLE h[8];for(unsigned i=0;i<8;i++){h[i]=CreateThread(NULL,0,worker,(LPVOID)(uintptr_t)i,0,NULL);assert(h[i]);}
 assert(WaitForMultipleObjects(8,h,TRUE,INFINITE)==WAIT_OBJECT_0);
 for(unsigned i=0;i<8;i++)CloseHandle(h[i]);assert(!s_pf_mem);
 assert(pf_reserve(2*PF_MAX));assert(!pf_reserve(1));pf_release(2*PF_MAX);assert(!s_pf_mem);
 puts("80000 concurrent reservation attempts: original two-block ceiling never exceeded, all bytes released");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(code);exe=p/'test.exe'
 subprocess.run([str(r.parent/'tools/mingw64/bin/gcc.exe'),'-std=c11','-O2',str(p/'test.c'),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
