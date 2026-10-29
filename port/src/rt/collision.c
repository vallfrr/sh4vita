// The game's invalid/null collision-resource path clears the header and polygon
// pointer, but leaves its BSP root pointing into the previous room's freed data.
#include "rt.h"
#include "collision_native.h"
#include <stdlib.h>

extern void F_00561a20_orig(CPU *c);
extern unsigned g_frame;
extern void F_0041d670_orig(CPU *c);
extern void F_00420a70(CPU *c), F_0041ac30(CPU *c);

// Only these two audited cdecl callers enter the native chain. Both discard
// EAX/ECX/EDX and x87 condition codes after this void call; callee-saved state,
// FPU stack/control and all externally read collision state remain intact.
// Recursion and any other entry keep the original path. The core has no CPU.
void nat_collision_sphere(CPU *c) {
    HOSTFN;
    static int enabled=-1;
    static unsigned target,verified,accepted,refused,failed;
    if(enabled<0) {
        const char *s=getenv("SH4_NATIVE_COLLISION"),*v=getenv("SH4_COLLISION_VERIFY");
        enabled=s && atoi(s)!=0;
        target=v ? (unsigned)strtoul(v,NULL,10) : 0;
    }
    if(!enabled || c!=g_main_cpu || c->esp<0x10000 || c->esp>g_mem_size-12) {
        F_0041d670_orig(c); return;
    }
    uint32_t ret=R32(c->esp),query=ARG(2);
    if(ret!=0x561ba2 && ret!=0x561f60) { F_0041d670_orig(c); return; }
    if(query>g_mem_size-20 || g_mem_size<0xed793c ||
       (query<c->esp+12 && (uint64_t)query+20>c->esp-0x10000) ||
       (query<0xed7a00 && (uint64_t)query+20>0xed7400) ||
       R32(0x5ce064)!=0 || R32(0x5b729c)!=0x3f800000 ||
       R32(0x5b79cc)!=0x3727c5ac || R32(0x5b79f4)!=0xb727c5ac ||
       rt_lookup(0x420a70)!=F_00420a70 || rt_lookup(0x41ac30)!=F_0041ac30) {
        ++refused; F_0041d670_orig(c); return;
    }
    Sh4CollisionView v={.data=g_mem,.size=g_mem_size,.root=ARG(1),.query_address=query,
                        .exclude_begin=c->esp-0x10000,.exclude_end=c->esp+12};
    Sh4CollisionQuery q={.count=R32(0xed7938)};
    memcpy(q.sphere,g_mem+query,20);
    memcpy(q.work,g_mem+0xed7460,sizeof q.work);
    memcpy(q.polygon,g_mem+0xed7538,sizeof q.polygon);
    if(!sh4_collision_sphere(&v,&q)) {
        ++refused; F_0041d670_orig(c); return;
    }
    ++accepted;
    if(verified<target) {
        // Transactional core has made no guest writes: run the original on the
        // same inputs, keep its result/CPU state, and compare the native output.
        F_0041d670_orig(c);
        unsigned bad=R32(0xed7938)!=q.count || memcmp(g_mem+query,q.sphere,20) ||
                     memcmp(g_mem+0xed7460,q.work,sizeof q.work) ||
                     memcmp(g_mem+0xed7538,q.polygon,sizeof q.polygon);
        ++verified;
        if(bad) { ++failed; enabled=0; }
        if(verified==1 || verified%10000==0 || verified==target || bad)
            rt_log("[collision-native] frame=%u verified=%u/%u failed=%u accepted=%u refused=%u nodes=%u polygons=%u\n",
                   g_frame,verified,target,failed,accepted,refused,q.nodes,q.polygons);
        return;
    }
    memcpy(g_mem+query,q.sphere,20);
    memcpy(g_mem+0xed7460,q.work,sizeof q.work);
    memcpy(g_mem+0xed7538,q.polygon,sizeof q.polygon);
    W32(0xed7938,q.count);
    c->esp+=4;
    if(accepted==1)
        rt_log("[collision-native] frame=%u enabled nodes=%u polygons=%u refused=%u\n",
               g_frame,q.nodes,q.polygons,refused);
}

void nat_collision_set(CPU *c) {
    F_00561a20_orig(c);
    HOSTFN;
    if (!R32(0x12f6d70) && R32(0x12f6d78)) {
        static int legacy = -1;
        if (legacy < 0) legacy = getenv("SH4_LEGACY_BSP") != NULL;
        rt_log("[collision] frame=%u %s stale BSP %08x after null/invalid resource\n",
               g_frame, legacy ? "retaining (SH4_LEGACY_BSP)" : "clearing", R32(0x12f6d78));
        if (!legacy) W32(0x12f6d78, 0);
    }
}
