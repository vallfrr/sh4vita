#include "rt.h"
#include "collision_native.h"
#include <stdlib.h>

extern void F_0041f570_orig(CPU *c);
extern void F_00420a70(CPU *c),F_0041ac30(CPU *c);
extern unsigned g_frame;

// 420c10 sets the ordered collector, then calls this list service. Other
// callers and recursive/BSP entry points retain their existing implementations.
void nat_collision_polygons(CPU *c) {
    HOSTFN;
    if(c!=g_main_cpu) { F_0041f570_orig(c);return; }
    static int enabled=-1,mapping_ok;
    static unsigned target,verified,accepted,refused,failed;
    if(enabled<0) {
        const char *s=getenv("SH4_NATIVE_SPHERE_LIST"),*v=getenv("SH4_SPHERE_LIST_VERIFY");
        enabled=s && !strcmp(s,"1");target=v ? (unsigned)strtoul(v,NULL,10) : 0;
        mapping_ok=!enabled || (rt_lookup(0x420a70)==F_00420a70 && rt_lookup(0x41ac30)==F_0041ac30);
    }
    uint32_t control=c->fcw|0x40u;
    if(!enabled || c->df || (control!=0x27f && control!=0x37f) ||
       c->esp<0x10000 || c->esp>g_mem_size-16 || g_mem_size<0xed7a00 ||
       R32(c->esp)!=0x420c33) { F_0041f570_orig(c);return; }
    uint32_t query=ARG(3);
    if(query>g_mem_size-20 ||
       (query<c->esp+16 && (uint64_t)query+20>c->esp-0x10000) ||
       (query<0xed7a00 && (uint64_t)query+20>0xed7400) ||
       R32(0xed7500)!=0x420a70 || !mapping_ok ||
       R32(0x5ce064)!=0 || R32(0x5b729c)!=0x3f800000 ||
       R32(0x5b79cc)!=0x3727c5ac || R32(0x5b79f4)!=0xb727c5ac) {
        ++refused;F_0041f570_orig(c);return;
    }
    Sh4CollisionView v={.data=g_mem,.size=g_mem_size,.query_address=query,
        .exclude_begin=c->esp-0x10000,.exclude_end=c->esp+16};
    Sh4CollisionQuery q;
    unsigned first=R32(0xed7938);q.count=first;
    memcpy(q.sphere,g_mem+query,sizeof q.sphere);
    memcpy(q.work,g_mem+0xed7460,sizeof q.work);
    if(!sh4_collision_polygons(&v,ARG(1),(int32_t)ARG(2),&q)) {
        ++refused;F_0041f570_orig(c);return;
    }
    ++accepted;
    if(verified<target) {
        uint8_t expected[0x600];memcpy(expected,g_mem+0xed7400,sizeof expected);
        memcpy(expected+0x60,q.work,sizeof q.work);
        // The service only appends candidates. Keep the existing prefix and
        // unused entries in place instead of copying all 256 entries twice.
        memcpy(expected+0x138+first*4,q.polygon+first,(q.count-first)*4);
        memcpy(expected+0x538,&q.count,4);
        F_0041f570_orig(c);
        int bad=memcmp(g_mem+0xed7400,expected,sizeof expected) ||
                memcmp(g_mem+query,q.sphere,sizeof q.sphere);
        ++verified;if(bad) {++failed;enabled=0;}
        if(verified==1 || verified%10000==0 || verified==target || bad)
            rt_log("[sphere-list] frame=%u verified=%u/%u failed=%u accepted=%u refused=%u polygons=%u hits=%u\n",
                g_frame,verified,target,failed,accepted,refused,q.polygons,q.count);
        return;
    }
    memcpy(g_mem+query,q.sphere,sizeof q.sphere);
    memcpy(g_mem+0xed7460,q.work,sizeof q.work);
    memcpy(g_mem+0xed7538+first*4,q.polygon+first,(q.count-first)*4);W32(0xed7938,q.count);
    c->eax=0;c->esp+=4;
    if(accepted==1)
        rt_log("[sphere-list] frame=%u enabled polygons=%u hits=%u refused=%u\n",g_frame,q.polygons,q.count,refused);
}
