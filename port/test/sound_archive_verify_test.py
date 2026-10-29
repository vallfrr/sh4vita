"""Compare the diagnostic streaming reader against all resident GOG entries."""
from pathlib import Path
import subprocess,tempfile,argparse
root=Path(__file__).resolve().parents[2]
ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--game',type=Path,default=root/'build/pc/game');args=ap.parse_args()
head=r'''
#include "rt.h"
#include "sound_archive.h"
#include "sound_archive_access.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
uint8_t *g_mem;uint32_t g_mem_size=32u<<20;CPU *g_main_cpu;char g_game_dir[512];
static unsigned failures,success;
void rt_sound_archive_verify(CPU *,uint32_t,uint32_t);
extern int g_sound_transfer_on;
int rt_sound_archive_transfer(CPU *,uint32_t,uint32_t,uint32_t);
void rt_log(const char *fmt,...) {if(strstr(fmt,"FAILED"))failures++;else if(strstr(fmt,"failed=0"))success++;}
int main(int argc,char **argv) {
 assert(argc==2);snprintf(g_game_dir,sizeof g_game_dir,"%s",argv[1]);
 char path[600];snprintf(path,sizeof path,"%s/sound/sound.pac",g_game_dir);
 FILE *f=fopen(path,"rb");assert(f);fseek(f,0,SEEK_END);long bytes=ftell(f);rewind(f);
 g_mem=calloc(1,g_mem_size);assert(g_mem);uint8_t *const mb=g_mem;
 uint32_t obj=0x2000,base=0x100000;assert(fread(mb+base,1,bytes,f)==(size_t)bytes);fclose(f);
 memcpy(mb+obj,mb+base+8,355*12);W32(obj+0x10ec,base);
 CPU main_cpu={0},other={0};g_main_cpu=&main_cpu;
 rt_sound_archive_verify(&other,obj,0);assert(!success&&!failures);
 uint8_t *copy=malloc(g_mem_size);assert(copy);memcpy(copy,mb,g_mem_size);
 CPU before=main_cpu;
 for(unsigned id=0;id<355;id++)rt_sound_archive_verify(&main_cpu,obj,id);
 int transfer=getenv("SH4_SOUND_ARCHIVE_TRANSFER")!=NULL;
 assert(success==(transfer?0u:23u)&&!failures&&!memcmp(&main_cpu,&before,sizeof before));
 assert(!memcmp(copy,mb,g_mem_size));
 g_sound_read_headers=1;
 for(unsigned id=0;id<355;id++) {
  unsigned a=base+R32(obj+12*id);
  assert(rt_sound_r8(mb,&main_cpu,a+22)==R8(a+22));
  assert(rt_sound_r16(mb,&main_cpu,a+20)==R16(a+20));
  assert(rt_sound_r32(mb,&main_cpu,a+24)==R32(a+24));
 }
 assert(!memcmp(copy,mb,g_mem_size));g_sound_read_headers=0;
 /* Delivery remains optional; no transfer is allowed before explicit opt-in. */
 if(!transfer){assert(!g_sound_transfer_on);assert(!rt_sound_archive_transfer(&main_cpu,0x8000,base+100,100));}
 g_sound_transfer_on=1;
 unsigned from=base+R32(obj+12*343)+44,n=R32(obj+12*343+4)-44;
 assert(n>65536 && n<2u<<20);
 assert(rt_sound_archive_transfer(&main_cpu,0x2000000-n,from,n));
 assert(!memcmp(mb+0x2000000-n,mb+from,n));
 assert(!rt_sound_archive_transfer(&other,0x8000,from,4));
 assert(!rt_sound_archive_transfer(&main_cpu,from+1,from,4));
 assert(!rt_sound_archive_transfer(&main_cpu,g_mem_size-1,from,4));
 memcpy(copy,mb,g_mem_size);
 /* A changed resident byte must be detected without changing it back. */
 unsigned at=base+R32(obj)+20;mb[at]^=0xff;copy[at]^=0xff;
 assert(!rt_sound_archive_transfer(&main_cpu,0x8000,base+R32(obj),100));
 assert(!g_sound_transfer_on && failures==1 && !memcmp(copy,mb,g_mem_size));
 rt_sound_archive_verify(&main_cpu,obj,0);assert(failures==1);
 free(copy);free(mb);
 puts("all 355 live-bank comparisons preserve CPU/memory; mismatch is detected and disables the observer");
}
'''
with tempfile.TemporaryDirectory(prefix='sh4-sound-shadow-') as d:
 c=Path(d)/'test.c';exe=Path(d)/'test.exe';c.write_text(head)
 subprocess.run(['gcc','-std=gnu11','-O2','-I'+str(root/'port/src/rt'),str(c),str(root/'port/src/rt/sound_archive.c'),str(root/'port/src/rt/sound_archive_cache.c'),str(root/'port/src/rt/sound_archive_verify.c'),'-o',str(exe)],check=True)
 subprocess.run([str(exe),str(args.game.resolve())],check=True)
 import os
 subprocess.run([str(exe),str(args.game.resolve())],env={**os.environ,'SH4_SOUND_ARCHIVE_TRANSFER':'1'},check=True)
