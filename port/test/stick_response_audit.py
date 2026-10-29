"""Measure the existing production response without changing the stick feel."""
from pathlib import Path
import argparse,json,os,shutil,subprocess,tempfile
root=Path(__file__).resolve().parents[2]
src=(root/'port/src/dx/dinput.c').read_text(encoding='utf-8')
functions=src[src.index('static int32_t axis('):src.index('// Each device independently waits')]
head=r'''
#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include <assert.h>
typedef int16_t Sint16;
typedef struct { int32_t rmin[8],rmax[8],dead; } DIDev;
'''
tail=r'''
int main(void) {
 DIDev pad={0};for(int i=0;i<8;i++){pad.rmin[i]=-1000;pad.rmax[i]=1000;}
 float scales[]={.36f,.55f,.9f,1.f,2.16f};printf("[");
 for(unsigned i=0;i<sizeof scales/sizeof scales[0];i++) {
  int first=0,digital=0,saturated=0,previous=0,max_asymmetry=0,asymmetric_samples=0;
  for(int raw=0;raw<=32767;raw++) {
   int value=axis(&pad,0,shape((Sint16)raw,scales[i]));
   int opposite=axis(&pad,0,shape((Sint16)-raw,scales[i]));
   assert(value>=previous && value<=1000 && opposite>=-1000 && opposite<=0);
   int asymmetry=abs(value+opposite);
   if(asymmetry>max_asymmetry)max_asymmetry=asymmetry;
   if(asymmetry>2)++asymmetric_samples;
   assert(raw || !value);
   if(value && !first)first=raw;
   if(value>300 && !digital)digital=raw;
   if(value==1000 && !saturated)saturated=raw;
   previous=value;
  }
  printf("%s{\"scale\":%.6g,\"first_nonzero_raw\":%d,\"first_above_digital_300_raw\":%d,\"saturation_raw\":%d,\"full_deflection_axis\":%d,\"maximum_sign_asymmetry\":%d,\"asymmetric_samples_above_two_units\":%d}",i?",":"",scales[i],first,digital,saturated,previous,max_asymmetry,asymmetric_samples);
 }
 puts("]");
}
'''
parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--out',type=Path);args=parser.parse_args()
with tempfile.TemporaryDirectory(prefix='sh4-stick-response-') as tmp:
    p=Path(tmp);(p/'test.c').write_text(head+functions+tail,encoding='utf-8')
    cc=os.environ.get('CC') or shutil.which('gcc') or 'J:/tools/mingw64/bin/gcc.exe'
    subprocess.run([cc,'-std=gnu11','-O2','-include','stdlib.h',str(p/'test.c'),'-lm','-o',str(p/'test.exe')],check=True)
    rows=json.loads(subprocess.check_output([str(p/'test.exe')],text=True))
    if args.out:args.out.write_text(json.dumps(rows,indent=2),encoding='utf-8')
    for row in rows:
        raw=row['first_nonzero_raw'];digital=row['first_above_digital_300_raw']
        print(f"scale={row['scale']:g}: first nonzero {100*raw/32767:.2f}%; digital >300 {100*digital/32767:.2f}%; full-deflection {row['full_deflection_axis']}; first saturation {row['saturation_raw'] or 'none'}")
        print(f"  sign asymmetry: at most {row['maximum_sign_asymmetry']} axis units; {row['asymmetric_samples_above_two_units']} input codes above two units")
    print("Production response monotonic. Hard dead-zone thresholds can magnify the signed SDL normalization difference; digital threshold is a separate consumer, not proof of the final camera dead zone.")
