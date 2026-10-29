"""Check the production backend selection, including the first disabled call."""
from pathlib import Path
import subprocess,tempfile,os
r=Path(__file__).resolve().parents[2];s=(r/'port/src/d3d8/resource.c').read_text(encoding='utf-8')
start=s.index('    static int hw_dxt = -1;')
end=s.index('\n',s.index('    int hw_texture=',start))
policy=s[start:end]
code=r'''
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include "d3d8/dxt16.h"
typedef struct { uint32_t w,h; } Texture;
static int production(uint32_t w,uint32_t h,unsigned skip){Texture value={w,h},*t=&value;
'''+policy+r'''
#endif
return hw_texture;
}
static int power(uint32_t x){for(unsigned i=0;i<32;i++)if(x==(UINT32_C(1)<<i))return 1;return 0;}
int main(void){
 assert(production(512,512,0)==(getenv("SH4_HWDXT")!=NULL));
 unsigned count=0;uint32_t dims[]={0,1,2,3,4,6,8,16,32,128,256,512,1024,2048,4096,8192,UINT32_MAX};
 for(unsigned a=0;a<sizeof dims/sizeof dims[0];a++)for(unsigned b=0;b<sizeof dims/sizeof dims[0];b++)for(unsigned skip=0;skip<34;skip++){
  uint32_t w=dims[a],h=dims[b];uint64_t divisor=UINT64_C(1)<<skip,bw=w/divisor,bh=h/divisor;
  int shape=power(w)&&power(h)&&bw>=4&&bh>=4&&bw<=2048&&bh<=2048;
  int want=getenv("SH4_HWDXT")!=NULL&&shape;
  assert(production(w,h,skip)==want);count++;
 }
 assert(!sh4_dxt_hardware_eligible(4,1024,1)); // Entire thin chain stays software.
 assert(!sh4_dxt_hardware_eligible(1025,1025,1)); // Even if selected dimensions are powers of two.
 printf("%u real upload-policy decisions pass, including first-call opt-out, NPOT/thin chains and mip boundaries\n",count);
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(code);exe=p/'test.exe'
 subprocess.run([str(r.parent/'tools/mingw64/bin/gcc.exe'),'-std=c11','-O2','-Wall','-Wextra','-Werror','-D__vita__','-I'+str(r/'port/src'),str(p/'test.c'),'-o',str(exe)],check=True)
 for value in [None,'1','0']:
  env={**os.environ};env.pop('SH4_HWDXT',None)
  if value is not None:env['SH4_HWDXT']=value
  print('Hardware option',value,flush=True);subprocess.run([str(exe)],check=True,env=env)
