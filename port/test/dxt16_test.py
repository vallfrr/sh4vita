"""Compare direct DXT1 packing with the actual production decoder and pack loop."""
from pathlib import Path
import shutil,subprocess,tempfile
root=Path(__file__).resolve().parents[2]
source=(root/'port/src/d3d8/resource.c').read_text(encoding='utf-8')
decode=source[source.index('static void dxt_to_rgba('):].split('\n}\n',1)[0]+'\n}\n'
pack=source[source.index('    static const uint8_t bayer[16]'):source.index('    uint64_t prep_t2')]
head='#include <stdint.h>\n#include <stdlib.h>\n#include <string.h>\n#include <assert.h>\n#include <stdio.h>\n#include "d3d8/dxt16.h"\n#define FMT_DXT1 1\n#define FMT_DXT5 5\nstatic uint32_t dxt_block(uint32_t f){return f==FMT_DXT1?8:16;}\n'
tail='static uint32_t seed=0x63148abc;\nstatic uint32_t rnd(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}\nint main(void){\n for(unsigned test=0;test<10000;test++){\n  unsigned lim=test<8000?33:257,w=1+rnd()%lim,h=1+rnd()%lim;\n  size_t n=(size_t)w*h,bytes=(size_t)((w+3)/4)*((h+3)/4)*8;\n  uint8_t *src=malloc(bytes),*copy=malloc(bytes);uint16_t *a=malloc((n+2)*2),*b=malloc((n+2)*2);assert(src&&copy&&a&&b);\n  for(size_t i=0;i<bytes;i++)src[i]=(uint8_t)rnd();\n  if(test%3==0)for(size_t i=0;i<bytes;i+=8)memcpy(src+i,src+i+2,2); // equal colors and transparent palette entries\n  memcpy(copy,src,bytes);a[0]=b[0]=0x1234;a[n+1]=b[n+1]=0x5678;\n  reference(src,a+1,w,h);sh4_dxt1_rgb565(src,b+1,w,h);\n  assert(!memcmp(src,copy,bytes)&&!memcmp(a,b,(n+2)*2));free(src);free(copy);free(a);free(b);\n }\n puts("DXT1:10000 cases byte-identical to production RGBA decode + dither, including partial blocks, transparent/equal palettes and guards");\n}\n'
reference="static void reference(const uint8_t *src,uint16_t *dst,unsigned w,unsigned h){uint8_t *tmp=malloc((size_t)w*h*4);assert(tmp);int alpha=0;dxt_to_rgba(FMT_DXT1,src,tmp,w,h);\n"+pack+"memcpy(dst,tmp,(size_t)w*h*2);free(tmp);\n}\n"
with tempfile.TemporaryDirectory() as td:
 p=Path(td);(p/'test.c').write_text(head+decode+reference+tail)
 cc=shutil.which('gcc') or 'J:/tools/mingw64/bin/gcc.exe'
 subprocess.run([cc,'-std=c11','-O2','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-I'+str(root/'port/src'),str(p/'test.c'),'-o',str(p/'test.exe')],check=True)
 subprocess.run([str(p/'test.exe')],check=True)
