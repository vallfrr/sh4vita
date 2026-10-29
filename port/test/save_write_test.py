"""Compare the native save transport with the actual GOG writer and MD5 code.

Only file transport, path construction and stack-cookie checking are stubbed.
The supported executable supplies the original MD5 padding bytes. No save from
the player is opened; all inputs and outputs live in a temporary directory.
"""
from pathlib import Path
import argparse
import hashlib
import os
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HEAD = r'''
#include "rt.h"
#include "x86_funcs.h"
#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
uint8_t *g_mem;
uint32_t g_mem_size=32u<<20;
char g_game_dir[512];
CPU *g_main_cpu;
static unsigned char reference[0xa9810];
static unsigned char disk[0xa9810];
static int staged_fail, original_calls, staged_calls;
static int native_mode;
static uint32_t last_error;
void nat_savewrite(CPU *c);
void F_00408030_orig(CPU *c);
void rt_log(const char *format,...) { (void)format; }
void w32_set_last_error(CPU *c,uint32_t e) { (void)c;last_error=e; }
void rt_save_failed(CPU *c,uint32_t e) { w32_set_last_error(c,e); }
void w32_host_path(const char *src,char *dst,size_t n) { snprintf(dst,n,"%s",src); }
void F_00407df0(CPU *c) { HOSTFN;W32(c->ecx,4);STDRET(0,1); }
void F_00407c10(CPU *c) { HOSTFN;strcpy(GSTR(ARG(1)),"save/sh4.sav");STDRET(1,1); }
void F_00575ed6(CPU *c) { c->esp+=4; }
void F_00401000(CPU *c) { c->esp+=4; }
void F_00575ef0(CPU *c) { c->esp+=4-c->eax; }
void F_00407ea0(CPU *c) { HOSTFN;memcpy(mb+ARG(1),disk,sizeof disk);STDRET(1,1); }
void F_00408030(CPU *c) { if(native_mode) nat_savewrite(c);else F_00408030_orig(c); }
void imp_KERNEL32_WriteFile(CPU *c) {
 HOSTFN;assert(ARG(1)==4 && ARG(3)==sizeof reference && !ARG(5));
 memcpy(reference,mb+ARG(2),sizeof reference);W32(ARG(4),sizeof reference);original_calls++;STDRET(5,1);
}
void imp_KERNEL32_CloseHandle(CPU *c) { HOSTFN;assert(ARG(1)==4);STDRET(1,1); }
int rt_staged_file_write(const char *path,const void *data,size_t size) {
 assert(!strcmp(path,"save/sh4.sav") && size==sizeof reference);
 assert(!memcmp(reference,data,size));staged_calls++;
 if(staged_fail) { errno=ENOSPC;return 0; }return 1;
}
uint32_t rt_guest_call(CPU *c,uint32_t fn,int count,...) {
 HOSTFN;uint32_t args[3],saved=c->esp;va_list ap;va_start(ap,count);
 for(int i=0;i<count;i++)args[i]=va_arg(ap,uint32_t);va_end(ap);
 for(int i=count-1;i>=0;i--) { c->esp-=4;W32(c->esp,args[i]); }
 c->esp-=4;W32(c->esp,0xfffffff0);
 switch(fn) {
 case 0x407c10:F_00407c10(c);break;
 case 0x415300:F_00415300(c);break;
 case 0x415bf0:F_00415bf0(c);break;
 case 0x415d30:F_00415d30(c);break;
 default:assert(!"unexpected guest dependency");
 }
 c->esp=saved;return c->eax;
}
static uint32_t word(const uint8_t *p) { uint32_t n;memcpy(&n,p,4);return n; }
static uint16_t half(const uint8_t *p) { uint16_t n;memcpy(&n,p,2);return n; }
static void load_image(const char *name) {
 FILE *f=fopen(name,"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);
 uint8_t *data=malloc(n);assert(data);assert(fread(data,1,n,f)==(size_t)n);fclose(f);
 g_mem=calloc(1,g_mem_size);assert(g_mem);
 unsigned pe=word(data+0x3c),opt=pe+24,base=word(data+opt+28),sections=opt+half(data+pe+20);
 for(unsigned i=0;i<half(data+pe+6);i++) {
  const uint8_t *s=data+sections+40*i;unsigned size=word(s+16),raw=word(s+20),address=base+word(s+12);
  assert((size_t)raw+size<=(size_t)n && address+size<=g_mem_size);memcpy(g_mem+address,data+raw,size);
 }free(data);
}
static CPU start(void) {
 CPU c={.esp=0x300000,.ecx=0x11000,.fs_base=0x10000,.ebx=11,.ebp=12,.esi=13,.edi=14};
 HOSTFN;W32(c.esp+4,0x1500000);W32(c.ecx,0);return c;
}
static void check_abi(CPU *c) {
 assert(c->esp==0x300008 && c->ebx==11 && c->ebp==12 && c->esi==13 && c->edi==14);
}
int main(int argc,char **argv) {
 assert(argc==3 || argc==4);load_image(argv[1]);uint8_t *mb=g_mem;uint32_t seed=123;
 if(argc==4) {
  assert(!_putenv_s("SH4_SAFE_SAVE","0"));
  CPU c=start();g_main_cpu=&c;nat_savewrite(&c);
  assert(c.eax==1 && original_calls==1 && !staged_calls);check_abi(&c);
  free(g_mem);puts("explicit diagnostic fallback calls original GOG writer");return 0;
 }
 assert(!_putenv_s("SH4_SAFE_SAVE","")); // default player path, no env.txt required
 for(int mode=0;mode<12;mode++) {
  for(unsigned i=0;i<sizeof reference;i++) {
   seed=seed*1664525+1013904223;mb[0x1500000+i]=mode==0?0:mode==1?255:(seed>>24);
  }
  CPU c=start();F_00408030_orig(&c);assert(c.eax==1);check_abi(&c);
  // Hash tail/signature are overwritten by both writers; perturb them to
  // require the native path to recompute rather than trust existing bytes.
  W32(0x1500000,0);memset(mb+0x1500000+0xa9800,0x5a,16);
  c=start();g_main_cpu=&c;nat_savewrite(&c);assert(c.eax==1);check_abi(&c);assert(!R32(0x11000));
 }
 assert(original_calls==12 && staged_calls==12);
 // Exercise the real read/modify/write callers: options and save slots.
 // Both paths start from the same synthetic disk, so all untouched slots and
 // options must survive byte-for-byte, including the GOG's MD5 update.
 memcpy(disk,reference,sizeof disk);
 CPU c;
 for(int mode=0;mode<4;mode++) {
  for(unsigned i=0;i<0x5a30;i++)mb[0x1600000+i]=(unsigned char)(i*3+mode);
  for(int native=0;native<2;native++) {
   c=start();g_main_cpu=&c;native_mode=native;
   W32(c.esp+4,mode?mode==1?0:mode==2?15:29:0x1600000);
   W32(c.esp+8,mode?0x1600000:0x5e4);
   if(mode) { W32(c.esp+12,0x5a30);F_00408730(&c); }
   else F_00408540(&c);
   assert(c.eax==1 && c.esp==0x300004); // these two callers are cdecl
   assert(c.ebx==11 && c.ebp==12 && c.esi==13 && c.edi==14);
  }
 }
 memcpy(mb+0x1500000,reference,sizeof reference);
 c=start();g_main_cpu=&c;staged_fail=1;nat_savewrite(&c);
 assert(!c.eax && last_error==112);check_abi(&c);
 staged_fail=0;c=start();g_main_cpu=NULL;nat_savewrite(&c);assert(!c.eax && last_error==87);
 c=start();g_main_cpu=&c;W32(c.ecx,4);nat_savewrite(&c);assert(!c.eax && last_error==87);
 c=start();g_main_cpu=&c;W32(c.esp+4,g_mem_size-8);nat_savewrite(&c);assert(!c.eax && last_error==998);
 FILE *f=fopen(argv[2],"wb");assert(f);assert(fwrite(reference,1,sizeof reference,f)==sizeof reference);fclose(f);
 free(g_mem);puts("save writer: 12 saves plus original options/slots 0,15,29 read-modify-write callers match GOG; ABI and error returns passed");
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, default=ROOT / 'build/pc/game/SILENT HILL 4.exe')
    args = parser.parse_args()
    assert hashlib.sha256(args.exe.read_bytes()).hexdigest() == 'cf7081a6c78dbe51f6dc2e1c9bd978d8486f1d7b59d7a3b88e027c9b39f21dd0'
    sources = '\n'.join((ROOT / f'port/gen/x86_f{i:03}.c').read_text(encoding='utf-8') for i in (2, 3, 8, 9))
    bodies = []
    for address in ('00408030', '00408540', '00408730', '004152f0', '00415300', '00415340', '00415bb0', '00415bf0', '00415d30'):
        name = f'F_{address}'
        if address == '00408030' and f'void {name}_orig(CPU *c) {{' in sources:
            name += '_orig'
        start = sources.index(f'void {name}(CPU *c) {{')
        following = re.search(r'\nvoid F_', sources[start + 1:])
        assert following
        body = sources[start:start + 1 + following.start()]
        if address == '00408030':
            body = body.replace('void F_00408030(', 'void F_00408030_orig(')
        bodies.append(body)
    compiler = os.environ.get('CC') or shutil.which('gcc') or 'J:/tools/mingw64/bin/gcc.exe'
    with tempfile.TemporaryDirectory(prefix='sh4-save-format-') as tmp:
        path = Path(tmp)
        (path / 'test.c').write_text(HEAD + '\n' + '\n'.join(bodies), encoding='utf-8')
        subprocess.run([compiler, '-std=gnu11', '-O2', '-I' + str(ROOT / 'port/src/rt'),
                        '-I' + str(ROOT / 'port/gen'),
                        '-I' + str(ROOT.parent / 'tools/sdl2/SDL2-2.32.10/x86_64-w64-mingw32/include/SDL2'),
                        str(path / 'test.c'), str(ROOT / 'port/src/rt/save_write.c'),
                        '-lm', '-o', str(path / 'test.exe')], check=True)
        subprocess.run([str(path / 'test.exe'), str(args.exe.resolve()), str(path / 'save.bin')], check=True)
        subprocess.run([str(path / 'test.exe'), str(args.exe.resolve()), str(path / 'save.bin'), 'original'], check=True)
        data = (path / 'save.bin').read_bytes()
        assert len(data) == 694288 and data[-16:] == hashlib.md5(data[:-16]).digest()
        print('Original and native output also match independent MD5 verification.')


if __name__ == '__main__':
    main()
