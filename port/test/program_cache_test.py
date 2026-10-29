"""Run the production get_prog cache lookup with a deterministic GL linker stub."""
from pathlib import Path
import argparse,re,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[2]
HEADER=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
typedef unsigned GLuint;
typedef int GLint;
#define MAX_STAGES 4
#define GLSL_PRE_VS ""
typedef struct { uint64_t source_hash; char *glsl; unsigned handle; } VShader;
typedef struct { uint64_t source_hash; char *glsl_body; unsigned handle; } PShader;
typedef struct { uint64_t options, ffp; } Device;
unsigned g_stat_progs;
unsigned g_pframe;
int g_audit_checks;
static unsigned links,sampler_sets;
static GLuint cur_prog;
void rt_log(const char *s,...) {(void)s;}
void rt_fatal(const char *s,...) {(void)s;abort();}
static char *strf(const char *fmt,...) {va_list a;va_start(a,fmt);int n=vsnprintf(NULL,0,fmt,a);va_end(a);char *s=malloc(n+1);va_start(a,fmt);vsnprintf(s,n+1,fmt,a);va_end(a);return s;}
static uint64_t mix64(uint64_t h,uint64_t v) {h^=v+0x9e3779b97f4a7c15ull+(h<<6)+(h>>2);return h;}
static uint64_t frag_opts(Device *d){return d->options;}
static uint64_t ffp_fkey(Device *d){return d->ffp;}
static char *ffp_vs(uint32_t fvf){return strf("ffp-vs-%u",fvf);}
static char *ffp_fs_body(Device *d){return strf("ffp-fs-%llu",(unsigned long long)d->ffp);}
static char *build_fs(Device *d,const char *body,uint64_t opts){(void)d;return strf("%s/%llu",body,(unsigned long long)opts);}
static GLuint link(const char *v,const char *f){assert(v&&f);return ++links;}
void glUseProgram(GLuint p){(void)p;}
GLint glGetUniformLocation(GLuint p,const char *n){(void)p;return n[0]=='s' && n[1]>='0' && n[1]<='3' ? (GLint)(0xd0000000u+(unsigned)(n[1]-'0')) : -1;}
void glUniform1i(GLint loc,int n){assert((unsigned)loc==0xd0000000u+(unsigned)n);sampler_sets++;}
'''
TEST=r'''
int main(int argc,char **argv) {
 (void)argv;
 Device d={0}; VShader v={11,"vertex-A",3};PShader f={21,"fragment-A",1};
 Prog *a=get_prog(&d,&v,0,&f);assert(a->prog==1&&get_prog(&d,&v,0,&f)==a);
 if(argc>1 && !strcmp(argv[1],"sign")){assert(sampler_sets==0);puts("Old uniform bug reproduced: valid negative sampler locations were skipped.");return 0;}
 if(argc==1)assert(sampler_sets==4);
 // Reallocate a different VS at exactly the same host address/guest handle.
 v.source_hash=12;v.glsl="vertex-B";
 Prog *b=get_prog(&d,&v,0,&f);
 if(argc>1){assert(b==a);puts("Old bug reproduced: a new shader at the same address selects the old program.");return 0;}
 assert(b!=a && links==2 && b->vsource==12);
 // Repeat for pixel shader, then both stages, as on death/reload.
 f.source_hash=22;f.glsl_body="fragment-B";Prog *c=get_prog(&d,&v,0,&f);assert(c!=b&&links==3);
 v.source_hash=13;v.glsl="vertex-C";f.source_hash=23;f.glsl_body="fragment-C";
 Prog *e=get_prog(&d,&v,0,&f);assert(e!=c&&links==4);
 // Same source at different addresses should reuse the existing GL program.
 VShader vcopy=v;PShader fcopy=f;assert(get_prog(&d,&vcopy,0,&fcopy)==e&&links==4);
 // An earlier program remains reusable with its correct constants/sampler metadata.
 v.source_hash=11;v.glsl="vertex-A";f.source_hash=21;f.glsl_body="fragment-A";
 assert(get_prog(&d,&v,0,&f)==a&&links==4);
 d.options=1;assert(get_prog(&d,&v,0,&f)!=a);d.options=0;
 // Fixed-function state and programmable stages have separate identities.
 v.source_hash=0x8000000000000077ull;v.glsl="vertex-D";
 Prog *fixed=get_prog(&d,NULL,0x77,&f);assert(get_prog(&d,&v,0,&f)!=fixed);
 f.source_hash=d.ffp;f.glsl_body="fragment-D";
 Prog *fixed_f=get_prog(&d,&v,0,NULL);assert(get_prog(&d,&v,0,&f)!=fixed_f);
 g_audit_checks=1;assert(get_prog(&d,&v,0,&f));
 puts("Program identity: VS/PS reuse, repeated source, reload, options and fixed-function namespaces passed.");
 return 0;
}
'''
def main():
 p=argparse.ArgumentParser();p.add_argument('--cc',default='gcc');p.add_argument('--source',type=Path,default=ROOT/'port/src/d3d8/draw.c');p.add_argument('--expect-alias',action='store_true');p.add_argument('--expect-sign-drop',action='store_true');a=p.parse_args()
 source=a.source.read_text(encoding='utf-8-sig');record=source[source.index('typedef struct Prog {'):source.index('static GLuint g_blit_prog')]
 start=source.index('static Prog *get_prog(');end=source.index('// ---------- cibles',start)
 with tempfile.TemporaryDirectory(prefix='sh4-program-cache-') as folder:
  c=Path(folder)/'cache.c';exe=Path(folder)/'cache.exe';helper=source[source.index('static int uniform_valid('):source.index('static int uniform_valid(')+source[source.index('static int uniform_valid('):].index('}')+1] if 'static int uniform_valid(' in source else '';c.write_text(HEADER+record+helper+source[start:end]+TEST,encoding='utf-8')
  subprocess.run([a.cc,'-std=gnu11','-O2',str(c),'-o',str(exe)],check=True)
  subprocess.run([str(exe)]+(['old'] if a.expect_alias else ['sign'] if a.expect_sign_drop else []),check=True)
if __name__=='__main__':main()
