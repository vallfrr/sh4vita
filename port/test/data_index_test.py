"""Portable metadata checker: complete, partial, malformed and missing transfers."""
from pathlib import Path
import os,shutil,subprocess,tempfile
root=Path(__file__).resolve().parents[2]
c=r'''
#include "data_index.h"
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
static unsigned calls;
static int lookup(void *user,const char *relative,uint64_t *bytes) {
 ++calls;char path[1024];snprintf(path,sizeof path,"%s/%s",(const char*)user,relative);
 struct stat s;if(stat(path,&s))return 0;if(!S_ISREG(s.st_mode))return 0;
 *bytes=(uint64_t)s.st_size;return 1;
}
int main(int argc,char **argv) {
 assert(rt_data_index_mode("{}")==0);
 assert(rt_data_index_mode("{\"file_index\":\"files.idx\"}")==1);
 assert(rt_data_index_mode("{\"file_index\" : \"../save\"}")==-1);
 assert(rt_data_index_mode("{\"other\":\"file_index\"}")==0);
 if(argc!=3)return 2;RtDataIndexReport report;
 int status=rt_data_index_check(argv[1],lookup,argv[2],&report);
 printf("%d|%u|%llu|%u|%llu|%llu|%s\n",status,report.files,(unsigned long long)report.bytes,
        calls,(unsigned long long)report.expected_size,(unsigned long long)report.actual_size,report.problem);
}
'''
def encoded(records):
    payload=''.join(f'{size}\t{name}\n' for size,name in records).encode()
    h=0xcbf29ce484222325
    for b in payload:h=((h^b)*0x100000001b3)&((1<<64)-1)
    return b'SH4FILES1\n'+payload+f'END\t{len(records)}\t{sum(r[0] for r in records)}\t{h:016x}\n'.encode()
def main():
    with tempfile.TemporaryDirectory(prefix='sh4-data-index-') as tmp:
        p=Path(tmp);game=p/'sh4';(game/'data').mkdir(parents=True)
        (game/'SILENT HILL 4.exe').write_bytes(b'ABCD');data=game/'data/test.bin';data.write_bytes(b'abcdefg')
        index=game/'files.idx';good=encoded([(4,'SILENT HILL 4.exe'),(7,'data/test.bin')])
        (p/'test.c').write_text(c,encoding='utf-8')
        cc=os.environ.get('CC') or shutil.which('gcc') or 'J:/tools/mingw64/bin/gcc.exe'
        subprocess.run([cc,'-std=gnu11','-O2','-Wall','-Wextra','-Werror','-Wno-misleading-indentation',
            '-I'+str(root/'port/src/rt'),str(p/'test.c'),str(root/'port/src/rt/data_index.c'),'-o',str(p/'test.exe')],check=True)
        def check(content,status,unconsulted=False):
            if content is not None:index.write_bytes(content)
            out=subprocess.check_output([str(p/'test.exe'),str(index),str(game)],text=True).strip().split('|')
            assert int(out[0])==status,out
            if unconsulted:assert int(out[3])==0,out
            return out
        check(None,0,True)
        row=check(good,1);assert row[1:4]==['2','11','2'],row
        for corrupt in (b'',good[:-1],good[:-20],good+b'garbage',good.replace(b'abcdefg',b'XXXXXXX').replace(b'7\tdata',b'8\tdata')):
            check(corrupt,-1,True)
        for name in ('../save/sh4.sav','save/sh4.sav','camera.cfg','env.txt','data/../save/sh4.sav','data//x','/data/x','data/x:y','data/a\\b'):
            check(encoded([(4,'SILENT HILL 4.exe'),(7,name)]),-1,True)
        check(encoded([(0,'data/only')]),-1,True)
        check(encoded([(0,'SILENT HILL 4.exe')]+[(0,f'data/{i}') for i in range(8192)]),-1,True)
        check(b'SH4FILES1\n18446744073709551616\tSILENT HILL 4.exe\n',-1,True)
        data.unlink();row=check(good,-2);assert row[-1]=='data/test.bin',row
        data.write_bytes(b'abcdefgh');row=check(good,-3);assert row[4:6]==['7','8'],row
        data.write_bytes(b'');check(good,-3)
        data.write_bytes(b'abcdefg');check(good,1)
        print('data index: legacy absence, valid counts, truncation/checksum, trailing bytes, safe paths, limits, missing file, wrong size and repair passed; malformed inventories never query game paths')

if __name__ == "__main__":
    main()
