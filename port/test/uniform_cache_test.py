"""Exercise the production render-queue uniform shadow, including packed-key collisions."""
from pathlib import Path
import argparse,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--cc',default='gcc');p.add_argument('--source',type=Path,default=ROOT/'port/src/d3d8/qgl.c');p.add_argument('--expect-alias',action='store_true');a=p.parse_args()
s=a.source.read_text(encoding='utf-8-sig');start=s.index('static struct { uint32_t',s.index('void q_glUseProgram('));end=s.index('void q_glUniform4f(',start)
head='''#include <stdint.h>\n#include <string.h>\n#include <assert.h>\n#include <stdio.h>\ntypedef int32_t GLint;\nstatic unsigned cur_prog,inv_gen=1;\n'''
test=r'''
int main(int argc,char **argv){
 (void)argv;uint32_t v[4]={5,6,7,8};
 cur_prog=1;assert(!uni_same(0x102000,v,4));assert(uni_same(0x102000,v,4));
 // Different (program,location) pairs can have identical packed XOR keys.
 cur_prog=2;int alias=uni_same(0x101000,v,4);
 if(argc>1){assert(alias);puts("Old queue bug reproduced: distinct uniforms alias through the packed key.");return 0;}
 assert(!alias);assert(uni_same(0x101000,v,4));
 v[3]++;assert(!uni_same(0x101000,v,4));assert(uni_same(0x101000,v,4));
 inv_gen++;assert(!uni_same(0x101000,v,4));
 assert(!uni_same((GLint)0xd0000001u,v,4));assert(uni_same((GLint)0xd0000001u,v,4));
 cur_prog=3;assert(!uni_same((GLint)0xd0000001u,v,4));
 puts("Uniform queue: exact program/location identity, values, generation and signed handles passed.");return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='sh4-uniform-shadow-') as folder:
 c=Path(folder)/'test.c';exe=Path(folder)/'test.exe';c.write_text(head+s[start:end]+test,encoding='utf-8')
 subprocess.run([a.cc,'-std=gnu11','-O2',str(c),'-o',str(exe)],check=True)
 subprocess.run([str(exe)]+(['old'] if a.expect_alias else []),check=True)
