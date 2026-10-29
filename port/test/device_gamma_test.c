// Exercises the real COM gamma setter and prep queue (sync/lazy/thread), without a GL context.
// Build as one translation unit with -O2 -fwhole-program and the normal SDL2 include/library paths.
// Put build/pc on PATH for its SDL2.dll; run device_gamma_test <sync|lazy|thread>.
#define SDL_MAIN_HANDLED
#include "../src/d3d8/device.c"
#include "../src/d3d8/prep.c"
#include <assert.h>
uint8_t *g_mem;
int g_audit_checks;
void rt_log(const char *fmt, ...) { (void)fmt; }
_Noreturn void rt_fatal(const char *fmt, ...) { fprintf(stderr,"%s\n",fmt);exit(42); }
void *com_get_kind(uint32_t guest,int kind) { return guest==1&&kind==K_DEVICE?g_dev:NULL; }
ComObj *com_get(uint32_t guest) { return guest==1?&g_dev->com:NULL; }
void gfx_bind_target(Device *d) {(void)d;}
void gfx_draw(Device *d,uint32_t p,uint32_t f,uint32_t nv,uint32_t np,const void *ind,int i32,uint32_t mi,const uint8_t *up,uint32_t us,const uint8_t *const *inl) {(void)d;}
void gfx_clear(Device *d,uint32_t f,uint32_t co,float z,uint32_t s,int n,const int32_t *re) {(void)d;}
void gfx_present(Device *d) {(void)d;}
void gfx_copy_gl(Surface *s,Surface *d,uint32_t n,const int32_t *r,const int32_t *p) {(void)s;}
void res_free(int k,void *p) {(void)k;}
void vs_delete(uint32_t h) {(void)h;}
void ps_delete(uint32_t h) {(void)h;}
static void check(void *expected) {assert(!memcmp(g_pdev->gamma,expected,sizeof g_pdev->gamma));}
int main(int argc,char **argv) {
 assert(argc==2);SDL_setenv("SH4_PREP",argv[1],1);assert(!SDL_Init(0));
 g_mem=calloc(1,65536);g_dev=calloc(1,sizeof *g_dev);g_dev->com.guest=1;default_states(g_dev,1);prep_init(g_dev);
 uint16_t ramp[3][256];
 for(unsigned pass=0;pass<3;pass++) {
  for(unsigned ch=0;ch<3;ch++)for(unsigned i=0;i<256;i++)ramp[ch][i]=(uint16_t)((pass==1?255-i:i)*(257-ch*50));
  memcpy(g_mem+0x4000,ramp,sizeof ramp);CPU c={0};c.esp=0x2000;uint8_t *mb=g_mem;
  W32(0x2004,1);W32(0x2008,0);W32(0x200c,0x4000);dev_SetGammaRamp(&c);assert(c.esp==0x2010);
  assert(!memcmp(g_dev->gamma,ramp,sizeof ramp));
  memset(g_mem+0x4000,0,sizeof ramp); // queue must own the submitted bytes, not the guest pointer
  prep_call(check,ramp);
 }
 puts("Gamma setter reached the real render-consumer state in order");return 0;
}
