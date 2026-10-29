// Differential test against generated reference functions extracted locally by
// tools/raytri_reference.py. No game-derived code is checked into this test.
#include "rt.h"
#include <assert.h>
#include <stdlib.h>

uint8_t *g_mem;
uint32_t g_mem_size = 24u << 20;
unsigned g_frame;
void rt_log(const char *fmt, ...) { (void)fmt; }
_Noreturn void rt_fatal(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap); exit(2);
}
void F_00573a20_orig(CPU *c), F_00401ec0(CPU *c), F_00401570(CPU *c), F_0041abc0(CPU *c);
void nat_raytri(CPU *c);
void rt_call(CPU *c, uint32_t fn) {
    switch (fn) {
    case 0x401ec0: case 0xdead0001: F_00401ec0(c); break;
    case 0x401570: F_00401570(c); break;
    case 0x41abc0: F_0041abc0(c); break;
    default: fprintf(stderr, "unexpected indirect call %08x\n", fn); abort();
    }
}
static uint32_t seed = 0x2309742;
static uint32_t rng(void) { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return seed; }
static float coord(void) { return ((int)(rng() % 200001) - 100000) * .03125f; }

int main(int argc, char **argv) {
    unsigned count = argc > 1 ? (unsigned)strtoul(argv[1], NULL, 10) : 1000000;
    g_mem = calloc(1, g_mem_size); assert(g_mem);
    uint8_t *const mb = g_mem;
    W32(0x13dc3dc, 0x401ec0); W32(0x13dc3e8, 0x401570); W32(0x13dc3ec, 0x41abc0);
    W32(0x5ce064, 0); W32(0x5b729c, 0x3f800000); W32(0x5b79cc, 0x3727c5ac);
    unsigned hits = 0;
    for (unsigned n = 0; n < count; n++) {
        uint32_t args[8] = {0x2000,0x2020,0x2040,0x2060,0x2080,0x2100,0x2104,0x2108};
        memset(mb + 0x2000, 0xa5, 512);
        for (unsigned i = 0; i < 5; i++) for (unsigned k = 0; k < 4; k++) W32(args[i] + k*4, rt_u32f(coord()));
        unsigned type = n & 7;
        if (type >= 1 && type <= 4) {
            float x = .25f, y = .375f, z = -2;
            if (type == 2) { static const float edge[] = {0, 1, -1e-8f, 1.00000012f, .5f}; x = edge[(n >> 3) % 5]; y = 0; }
            if (type == 3) z = 2;
            const float values[5][3] = {{x,y,z},{0,0,1},{0,0,0},{1,0,0},{0,1,0}};
            for (unsigned i=0;i<5;i++) for(unsigned k=0;k<3;k++) W32(args[i]+k*4,rt_u32f(values[i][k]));
            if (type == 4) memcpy(mb + args[4], mb + args[3], 12);
        } else if (type == 5) {
            static const uint32_t exceptional[] = {0x7fc00001,0xff800000,0x7f800000,1,0x80000001,0x7f7fffff};
            W32(args[(n >> 3) % 5] + (rng() % 3)*4, exceptional[(n >> 6) % 6]);
        } else if (type == 6) {
            for (unsigned i=0;i<5;i++) for(unsigned k=0;k<3;k++) W32(args[i]+k*4,rng());
        } else if (type == 7) {
            unsigned i = rng()%5, j = rng()%5;
            if (i != j) args[i] = args[j] + 4 * (rng()%3);
            if ((n & 63) == 7) args[2]++; // unaligned inputs must retain original semantics
        }
        for (unsigned i=5;i<8;i++) if (n & (1u << (i+1))) args[i]=0;
        if (n%127 == 0) args[5] = args[1] + 4;
        if (n%131 == 0) args[6] = args[7] = 0x2100;
        W32(0x13dc3dc, n%257 == 0 ? 0xdead0001 : 0x401ec0);
        unsigned char initial[512], expected[512];
        memcpy(initial, mb+0x2000, 512);
        CPU initial_cpu = {.eax=0x22222222,.ebx=0x11111111,.esi=0x33333333,.edi=0x44444444,
                           .ebp=0x55555555,.esp=0x30000,.ftop=n%8,.fcw=0x37f,.fsw=0x200};
        for (unsigned i=0;i<8;i++) initial_cpu.st[i]=100+i;
        W32(initial_cpu.esp, 0x12345678);
        for (unsigned i=0;i<8;i++) W32(initial_cpu.esp+4+i*4,args[i]);
        CPU ref = initial_cpu; F_00573a20_orig(&ref);
        memcpy(expected,mb+0x2000,512);
        memcpy(mb+0x2000,initial,512);
        CPU fast = initial_cpu; nat_raytri(&fast);
        int ok = ref.eax==fast.eax && ref.esp==fast.esp && ref.ebx==fast.ebx && ref.esi==fast.esi &&
                 ref.edi==fast.edi && ref.ebp==fast.ebp && ref.ftop==fast.ftop && ref.fsw==fast.fsw &&
                 !memcmp(expected,mb+0x2000,512);
        for(unsigned i=0;i<6;i++) {
            unsigned slot = (initial_cpu.ftop+i)%8;
            ok &= !memcmp(&ref.st[slot],&fast.st[slot],sizeof(double));
        }
        if (!ok) {
            fprintf(stderr,"DIFF case=%u type=%u seed=%08x eax=%u/%u fsw=%x/%x esp=%x/%x\n",n,type,seed,ref.eax,fast.eax,ref.fsw,fast.fsw,ref.esp,fast.esp);
            for(unsigned i=0;i<512;i++) if(expected[i]!=mb[0x2000+i]) { fprintf(stderr,"first byte +%u = %02x/%02x\n",i,expected[i],mb[0x2000+i]);break; }
            return 1;
        }
        hits += fast.eax != 0;
    }
    printf("%u ray/triangle cases, %u hits, zero return/output/FPU/control-state differences\n",count,hits);
    free(g_mem); return 0;
}
