"""Exercise the compiled-stage cache from a patched vitaGL checkout on a POSIX host."""
from pathlib import Path
import argparse
import subprocess, tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('checkout', type=Path)
args = parser.parse_args()
root = args.checkout.resolve()
s = (root/'source/custom_shaders.c').read_text(encoding='utf-8')
code = s[s.index('// Cache exact translated Cg'):s.index('#define vgl_compile_shader', s.index('// Cache exact translated Cg'))]
stub = r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#define XXH_STATIC_LINKING_ONLY
#define XXH_IMPLEMENTATION
#include "utils/xxhash_utils.h"
typedef unsigned char GLboolean;
typedef int SceUID;
typedef off_t SceOff;
#define GL_TRUE 1
#define GL_FALSE 0
#define GL_VERTEX_SHADER 1
#define SCE_GXM_MAX_TEXTURE_UNITS 16
#define SCE_O_RDONLY O_RDONLY
#define SCE_O_CREAT O_CREAT
#define SCE_O_WRONLY O_WRONLY
#define SCE_O_TRUNC O_TRUNC
#define SCE_SEEK_END SEEK_END
#define SCE_SEEK_SET SEEK_SET
#define sceIoMkdir mkdir
#define sceIoOpen open
#define sceIoRead read
#define sceIoWrite write
#define sceIoClose close
#define sceIoLseek lseek
#define vglMalloc malloc
#define vgl_free free
typedef struct { char texcoord_names[10][64], color_names[2][64]; GLboolean texcoord_used[10], color_used[2]; } binds_map;
typedef struct { char name[64]; } glsl_samplers_info;
typedef struct { unsigned type, size; char *source; binds_map semantics; void *prog; } shader;
static unsigned driver_calls;
static unsigned compiler_opts=3,compiler_fastmath=1,compiler_fastprecision=0,compiler_fastint=1;
static char vgl_shader_cache_path[64]="cache";
void rt_log(const char *fmt, ...) {}
const char *sh4_shader_bundle_stage_root(void) { return "bundle"; }
static void unserialize_shader(void *p,size_t n,shader *s,GLboolean binds) { assert(p&&n&&!binds);driver_calls++;s->prog=(void*)1; }
'''
tests = r'''
static unsigned observed;
static void observe(const char *name) { assert(strstr(name, "cg2-")); observed++; }
static shader fresh(void) { shader s={0};s.type=1;s.source=strdup("CG stage");s.size=strlen(s.source);return s; }
static unsigned char payload[4+1+32];
static size_t payload_size=4+32;
static void valid(shader *s,const char *name) { memset(payload,0,sizeof payload);sh4_cg_save(s,name,payload,payload_size); }
static void reject(shader *s,const char *name) { unsigned n=driver_calls; assert(!sh4_cg_load(s,name));assert(driver_calls==n&&s->source);assert(!sh4_cg_cache_validate(name)); }
int main(void) {
#ifdef HAVE_GLSL_TEXTURE_SIZE
 payload_size++;
#endif
 shader s=fresh();char name[256];sh4_cg_name(&s,name);mkdir("cache",0777);mkdir("cache/v",0777);char parent[256];strcpy(parent,name);*strrchr(parent,'/')=0;mkdir(parent,0777);
 sh4_cg_cache_observe(observe);
 reject(&s,name);assert(!sh4_cg_cache_validate(name));assert(rmdir(parent)==0);valid(&s,name);
 assert(observed==1 && sh4_cg_cache_validate(name) && driver_calls==0);
 assert(!sh4_cg_cache_validate("outside/cg2-1234567890123456.gxp"));
 assert(unlink(name)==0&&rmdir(parent)==0&&rmdir("cache/v")==0&&rmdir("cache")==0);
 valid(&s,name);assert(sh4_cg_cache_validate(name));
 uint64_t config=sh4_cg_cache_config();compiler_fastmath^=1;assert(config!=sh4_cg_cache_config());compiler_fastmath^=1;strcpy(s.semantics.texcoord_names[9],"unused-current");binds_map keep=s.semantics;char other[256];sh4_cg_name(&s,other);assert(!strcmp(name,other));assert(sh4_cg_load(&s,name)&&!s.source&&driver_calls==1);assert(!memcmp(&s.semantics,&keep,sizeof keep));printf("KEY=%s\n",name);
 s=fresh();valid(&s,name);int f=open(name,O_RDWR);assert(f>=0);unsigned char flip=0xff;lseek(f,sizeof(sh4_cg_header)+6,SEEK_SET);write(f,&flip,1);close(f);reject(&s,name);
 valid(&s,name);f=open(name,O_RDWR);ftruncate(f,sizeof(sh4_cg_header)+payload_size-1);close(f);reject(&s,name);
 for(unsigned k=0;k<4;k++){valid(&s,name);f=open(name,O_RDWR);uint64_t value=k==0?0:k==1?0xffffffffu:k==2?0:1;lseek(f,k==0?0:k==1?4:k==2?8:16,SEEK_SET);write(f,&value,k<2?4:8);close(f);reject(&s,name);}
 valid(&s,name);uint32_t bad=0xffffffffu;memcpy(payload,&bad,4);sh4_cg_save(&s,name,payload,payload_size);reject(&s,name);
#ifdef HAVE_GLSL_TEXTURE_SIZE
 valid(&s,name);payload[4]=17;sh4_cg_save(&s,name,payload,payload_size);reject(&s,name);
#endif

 valid(&s,name);
 char bundled[512];snprintf(bundled,sizeof bundled,"bundle/%s",name+strlen(vgl_shader_cache_path)+1);
 sh4_cache_parent(bundled);
 unsigned char copy[4096];FILE *input=fopen(name,"rb");assert(input);size_t copied=fread(copy,1,sizeof copy,input);assert(copied>24);fclose(input);
 FILE *output=fopen(bundled,"wb");assert(output);assert(fwrite(copy,1,copied,output)==copied);assert(!fclose(output));
 assert(!unlink(name));assert(sh4_cg_cache_validate(name));
 shader from_bundle=fresh();binds_map original_map=from_bundle.semantics;
 assert(sh4_cg_load(&from_bundle,name)&&!memcmp(&from_bundle.semantics,&original_map,sizeof original_map));
 assert(!truncate(bundled,1));assert(!sh4_cg_cache_validate(name));
 valid(&s,name);assert(sh4_cg_cache_validate(name));assert(!unlink(name));
 output=fopen(bundled,"wb");assert(output);assert(fwrite(copy,1,copied,output)==copied);assert(!fclose(output));
 assert(!setenv("SH4_BUNDLED_CACHE","0",1));assert(!sh4_cg_cache_validate(name));
 assert(!setenv("SH4_BUNDLED_CACHE","1",1));assert(sh4_cg_cache_validate(name));
 assert(!unlink(bundled));
 assert(observed>0);sh4_cg_cache_observe(NULL);
 free(s.source);puts("Actual Cg cache loader: valid record, CRC/key/length checks and metadata bounds passed");return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='sh4-cg-cache-') as temp:
    folder = Path(temp)
    keys=[]
    for samplers in (False, True):
        run = folder/str(int(samplers));run.mkdir()
        c = run/'test.c'; c.write_text(stub+code+tests,encoding='utf-8')
        flags = ['-DHAVE_GLSL_TEXTURE_SIZE'] if samplers else []
        subprocess.run(['gcc','-O2','-Wall','-Wno-address',*flags,'-I'+str(root/'source'),str(c),'-o',str(run/'test')],check=True)
        result=subprocess.run([str(run/'test')],cwd=run,check=True,capture_output=True,text=True)
        print(result.stdout,end='')
        keys.append(result.stdout.splitlines()[0])
    assert keys[0]!=keys[1], 'Cache keys must isolate sampler metadata formats'
    print('Bundle-only stage load, corrupt-bundle fallback, disabled bundle, current semantics and metadata-format keys passed')
