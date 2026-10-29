"""Mock Vita directory metadata: reuse, sorting, lookup, errors and cleanup."""
from pathlib import Path
import os,shutil,subprocess,tempfile
from data_index_test import encoded
root=Path(__file__).resolve().parents[2]
types=r'''
#pragma once
#include <stdint.h>
typedef int SceUID;
typedef struct { unsigned st_mode; int64_t st_size; } SceIoStat;
typedef struct { SceIoStat d_stat; char d_name[256]; } SceIoDirent;
#define SCE_S_ISREG(m) (((m)&0xf000)==0x2000)
int sceIoGetstat(const char *,SceIoStat *);
SceUID sceIoDopen(const char *);
int sceIoDread(SceUID,SceIoDirent *);
int sceIoDclose(SceUID);
'''
driver=r'''
#include "rt.h"
#include "data_index.h"
#include <psp2/io/dirent.h>
#include <stdlib.h>
#include <assert.h>
static int mode,opens,reads,closes,stats,position,allocations,live;
void rt_log(const char *format,...){(void)format;}
void *mock_realloc(void *old,size_t bytes) {
 ++allocations;
 if((mode==4 && allocations==1)||(mode==5 && allocations==2))return NULL;
 void *p=realloc(old,bytes);if(p && !old)++live;return p;
}
void mock_free(void *p){if(p){--live;free(p);}}
int sceIoGetstat(const char *path,SceIoStat *s) {
 assert(!strcmp(path,"ROOT/SILENT HILL 4.exe"));++stats;s->st_mode=0x2000;s->st_size=4;return 0;
}
SceUID sceIoDopen(const char *path) {
 assert(!strcmp(path,"ROOT/data"));++opens;position=0;return mode==1?-1:9;
}
int sceIoDread(SceUID fd,SceIoDirent *e) {
 assert(fd==9);++reads;
 if(mode==2 && position==50)return -1;
 memset(e,0,sizeof *e);e->d_stat.st_mode=0x2000;
 if(position==0){strcpy(e->d_name,"z.bin");e->d_stat.st_size=30;}
 else if(position==1){strcpy(e->d_name,mode==6?"absent.bin":"TeSt.bin");e->d_stat.st_size=mode==7?8:7;}
 else if(position<102){snprintf(e->d_name,sizeof e->d_name,"extra-%03d.bin",position);e->d_stat.st_size=1;}
 else if(position==102){strcpy(e->d_name,"subdir");e->d_stat.st_mode=0x1000;}
 else return 0;
 ++position;return 1;
}
int sceIoDclose(SceUID fd){assert(fd==9);++closes;return mode==3?-1:0;}
int main(int argc,char **argv) {
 assert(argc==2);
 for(mode=0;mode<8;mode++) {
  opens=reads=closes=stats=position=allocations=live=0;
  RtDataIndexReport report;int result=rt_vita_data_index_check(argv[1],"ROOT",&report);
  assert(!live && opens==1 && stats==1 && closes==(mode==1?0:1));
  if(!mode){assert(result==1 && report.files==3 && report.bytes==41 && allocations==2);}
  else if(mode<6)assert(result==-4);
  else if(mode==6)assert(result==-2 && !strcmp(report.problem,"data/test.bin"));
  else assert(result==-3 && report.expected_size==7 && report.actual_size==8);
 }
 puts("Vita directory cache: one directory scan for sibling lookups, ASCII case handling, sorted lookup, open/read/close/two allocation failures, missing/wrong size and no retained allocations passed");
}
'''
with tempfile.TemporaryDirectory(prefix='sh4-directory-cache-') as tmp:
    p=Path(tmp);(p/'psp2/io').mkdir(parents=True)
    (p/'psp2/io/types.h').write_text(types)
    for name in ('stat.h','dirent.h'):(p/'psp2/io'/name).write_text('#include "types.h"\n')
    (p/'driver.c').write_text(driver);(p/'files.idx').write_bytes(encoded([(4,'SILENT HILL 4.exe'),(7,'data/test.bin'),(30,'data/z.bin')]))
    cc=os.environ.get('CC') or shutil.which('gcc') or 'J:/tools/mingw64/bin/gcc.exe'
    flags=['-std=gnu11','-O2','-Wall','-Wextra','-Werror','-I'+str(root/'port/src/rt'),'-I'+str(p)]
    subprocess.run([cc,*flags,'-Drealloc=mock_realloc','-Dfree=mock_free','-c',str(root/'port/src/vita/data_files.c'),'-o',str(p/'cache.o')],check=True)
    subprocess.run([cc,*flags,str(p/'driver.c'),str(root/'port/src/rt/data_index.c'),str(p/'cache.o'),'-lm','-o',str(p/'test.exe')],check=True)
    subprocess.run([str(p/'test.exe'),str(p/'files.idx')],check=True)
