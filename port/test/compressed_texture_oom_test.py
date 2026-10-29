from pathlib import Path
import subprocess,tempfile,sys
if len(sys.argv)!=2:raise SystemExit('Usage: python3 port/test/compressed_texture_oom_test.py /path/to/patched/vitaGL/source/utils/gpu_utils.c (requires GCC + ASan/UBSan)')
s=Path(sys.argv[1]).read_text();a=s.index('void gpu_alloc_compressed_texture(');z=s.index('\nvoid gpu_alloc_mipmaps(',a)
code=r'''
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
typedef int GLboolean;typedef int SceGxmTextureFormat;
enum {TEX_VALID=1,OBJ_NOT_USED=99,SCE_GXM_TEXTURE_FORMAT_UBC2_ABGR=10,SCE_GXM_TEXTURE_FORMAT_UBC3_ABGR,SCE_GXM_TEXTURE_FORMAT_UBC5_GR,SCE_GXM_TEXTURE_FORMAT_PVRT2BPP_1BGR,SCE_GXM_TEXTURE_FORMAT_PVRT2BPP_ABGR,SCE_GXM_TEXTURE_FORMAT_PVRT4BPP_1BGR,SCE_GXM_TEXTURE_FORMAT_PVRT4BPP_ABGR,SCE_GXM_TEXTURE_FORMAT_ETC1_1BGR,SCE_GXM_TEXTURE_FORMAT_PVRTII2BPP_ABGR};
typedef struct{unsigned w,h;} GT;
typedef struct{int status,mip_count,use_mips,last_frame;GT gxm_tex;void *data,*palette_data;} texture;
static int fail_realloc,fail_alloc,free_calls,copy_calls,init_calls,swap_calls;
static unsigned nearest_po2(unsigned v){unsigned p=1;while(p<v)p*=2;return p;}
static int gpu_get_compressed_mip_size(int l,unsigned w,unsigned h,int fmt){(void)l;(void)fmt;return w*h/2;}
static int gpu_get_compressed_mip_offset(int l,unsigned w,unsigned h,int fmt){(void)fmt;return l?w*h/2:0;}
static int gpu_get_compressed_mipchain_size(int l,unsigned w,unsigned h,int fmt){(void)fmt;return w*h/2+(l?8:0);}
static void gpu_free_texture_data(texture *t){assert(t->data);free(t->data);t->data=NULL;t->status=0;free_calls++;}
static void vglGetTexSizes(GT *t,unsigned *w,unsigned *h){*w=t->w;*h=t->h;}
static void *vgl_realloc(void *p,size_t n){return fail_realloc?NULL:realloc(p,n);}
static void *gpu_alloc_mapped_for_gpu(size_t n){return fail_alloc?NULL:malloc(n);}
static void vgl_memcpy(void *d,const void *s,size_t n){assert(d&&s);memcpy(d,s,n);copy_calls++;}
#define vgl_fast_memcpy vgl_memcpy
#define vgl_memset memset
#define MIN(a,b) ((a)<(b)?(a):(b))
#define ALIGNBLOCK(a,b) (((a)+(b)-1)&~((b)-1))
static unsigned tex_format_to_alignment(int f){(void)f;return 8;}
static void dxt_compress(void *d,uint8_t *s,unsigned w,unsigned h,int a){(void)d;(void)s;(void)w;(void)h;(void)a;assert(0);}
static void SwizzleTexData64Bpp(uint8_t *d,uint8_t *s,int x,int y,unsigned w,unsigned h,unsigned stride,unsigned tile){assert(!x&&!y&&w==1&&h==1&&stride==1&&tile==1);assert(d&&s);memcpy(d,s,8);swap_calls++;}
static void SwizzleTexData128Bpp(uint8_t *d,uint8_t *s,int x,int y,unsigned w,unsigned h,unsigned stride,unsigned tile){(void)d;(void)s;(void)x;(void)y;(void)w;(void)h;(void)stride;(void)tile;assert(0);}
#define SwizzleTexDataETC1 SwizzleTexData64Bpp
#define SCE_GXM_TRANSFER_COLORKEY_NONE 0
#define SCE_GXM_TRANSFER_FORMAT_RAW64 0
#define SCE_GXM_TRANSFER_LINEAR 0
#define SCE_GXM_TRANSFER_SWIZZLED 0
#define sceGxmTransferCopy(...) assert(0)
static void *gpu_alloc_mapped_temp(size_t n){(void)n;assert(0);return NULL;}
static void vglInitSwizzledTexture(GT *t,void *p,int fmt,unsigned w,unsigned h,int count){(void)fmt;assert(p&&count>=0);t->w=w;t->h=h;init_calls++;}
'''+s[a:z]+r'''
int main(void){
 uint8_t source[8]={3,4,5,6,7,8,9,10};
 for(unsigned trial=0;trial<200;trial++)for(int mode=0;mode<4;mode++){
  texture t={.status=TEX_VALID,.mip_count=1,.use_mips=1,.gxm_tex={8,8},.data=malloc(32)};assert(t.data);memset(t.data,(unsigned char)trial,32);
  void *old=t.data;free_calls=copy_calls=init_calls=swap_calls=0;fail_realloc=mode!=2;fail_alloc=mode==0;
  if(mode==3){t.data=realloc(t.data,40);assert(t.data);t.mip_count=2;old=t.data;}
  gpu_alloc_compressed_texture(1,4,4,1,8,source,&t,0,0);
  if(mode==0){assert(t.data==old&&t.mip_count==1&&t.status==TEX_VALID);assert(!free_calls&&!copy_calls&&!init_calls&&!swap_calls);}
  else {assert(t.status==TEX_VALID&&t.mip_count==2&&init_calls==1&&swap_calls==1);assert(!memcmp((uint8_t*)t.data+32,source,8));assert(free_calls==(mode==1));assert(copy_calls==(mode==1));}
  for(unsigned i=0;i<32;i++)assert(((uint8_t*)t.data)[i]==(unsigned char)trial);free(t.data);
 }
 texture t={0};fail_alloc=1;init_calls=swap_calls=0;gpu_alloc_compressed_texture(0,4,4,1,8,source,&t,0,0);assert(!t.data&&!init_calls&&!swap_calls);
 puts("800 production mip growth/rewrite/fault cases: double failure retains old chain; successful fallback copies before freeing; initial failure is safe");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(code)
 subprocess.run(['gcc','-std=c11','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
