"""Inject storage failures into the real staged writer, using disposable files."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'port/src/rt/staged_file.c').read_text(encoding='utf-8')
function = source[source.index('int rt_staged_file_write('):]
head = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <windows.h>
static int fault;
static FILE *test_open(const char *p, const char *m) {
 if(fault==1) { errno=EACCES;return NULL; } return fopen(p,m);
}
static size_t test_write(const void *p,size_t s,size_t n,FILE *f) {
 if(fault==2) { size_t r=fwrite(p,s,n/2,f);errno=ENOSPC;return r; } return fwrite(p,s,n,f);
}
static int test_flush(FILE *f) {
 if(fault==3) { errno=ENOSPC;return EOF; } return fflush(f);
}
static int test_close(FILE *f) {
 int r=fclose(f);if(fault==4) { errno=EIO;return EOF; }return r;
}
static BOOL test_replace(const char *a,const char *b,DWORD flags) {
 if(fault==5) return FALSE;
 return MoveFileExA(a,b,flags);
}
#define fopen test_open
#define fwrite test_write
#define fflush test_flush
#define fclose test_close
#define MoveFileExA test_replace
'''
tail = r'''
#undef fopen
#undef fwrite
#undef fflush
#undef fclose
#undef MoveFileExA
static unsigned char before[694288], after[694288], actual[694288];
static void old_save(void) {
 FILE *f=fopen("sh4.sav","wb");assert(f);
 assert(fwrite(before,1,sizeof before,f)==sizeof before);assert(!fclose(f));
}
static void check(const unsigned char *expected) {
 FILE *f=fopen("sh4.sav","rb");assert(f);
 assert(fread(actual,1,sizeof actual,f)==sizeof actual);assert(fgetc(f)==EOF);assert(!fclose(f));
 assert(!memcmp(actual,expected,sizeof actual));
 assert(GetFileAttributesA("sh4.sav.sh4stage")==INVALID_FILE_ATTRIBUTES);
}
int main(void) {
 for(unsigned i=0;i<sizeof before;i++) { before[i]=(unsigned char)i;after[i]=(unsigned char)(i*7+1); }
 for(int mode=1;mode<=5;mode++) {
  fault=0;old_save();fault=mode;
  assert(!rt_staged_file_write("sh4.sav",after,sizeof after));
  assert(errno==(mode==1?EACCES:mode==2||mode==3?ENOSPC:EIO));check(before);
 }
 fault=0;assert(rt_staged_file_write("sh4.sav",after,sizeof after));check(after);
 assert(!remove("sh4.sav"));assert(rt_staged_file_write("sh4.sav",before,sizeof before));check(before);
 // An interrupted stage is never selected as the good save; retry replaces it.
 FILE *f=fopen("sh4.sav.sh4stage","wb");assert(f);fputs("interrupted",f);fclose(f);
 assert(rt_staged_file_write("sh4.sav",after,sizeof after));check(after);
 assert(!rt_staged_file_write(NULL,after,sizeof after) && errno==EINVAL);
 assert(!rt_staged_file_write("unused",NULL,1) && errno==EINVAL);
 char long_path[700];memset(long_path,'x',sizeof long_path-1);long_path[699]=0;
 assert(!rt_staged_file_write(long_path,after,sizeof after) && errno==ENAMETOOLONG);
 puts("staged saves: previous 694288-byte save retained after open/write/flush/close/replace failures; success, new save and interrupted stage retry passed");
}
'''
compiler = os.environ.get('CC') or shutil.which('gcc') or 'J:/tools/mingw64/bin/gcc.exe'
with tempfile.TemporaryDirectory(prefix='sh4-staged-save-') as tmp:
    path = Path(tmp)
    (path / 'test.c').write_text(head + function + tail, encoding='utf-8')
    subprocess.run([compiler, '-std=gnu11', '-O2', '-Wall', str(path / 'test.c'),
                    '-o', str(path / 'test.exe')], check=True)
    subprocess.run([str(path / 'test.exe')], cwd=path, check=True)
