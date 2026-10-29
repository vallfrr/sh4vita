#include "rt.h"
#include "collision_segment.h"
#include <stdlib.h>

extern void F_004203a0_orig(CPU *c);
extern void F_00420a70(CPU *c),F_00420a90(CPU *c),F_00420b50(CPU *c);
extern void F_00401ec0(CPU *c),F_00401570(CPU *c),F_0041abc0(CPU *c);
extern void F_0041f360(CPU *c),F_0041eac0(CPU *c),F_0041ac30(CPU *c);
extern unsigned g_frame;

static int mappings_match(void) {
    // These RVAs resolve through the compile-time const x86_functab, never
    // through the dynamic host-function range. Their identity cannot change.
    return rt_lookup(0x420a70)==F_00420a70 && rt_lookup(0x420a90)==F_00420a90 &&
        rt_lookup(0x420b50)==F_00420b50 && rt_lookup(0x401ec0)==F_00401ec0 &&
        rt_lookup(0x401570)==F_00401570 && rt_lookup(0x41abc0)==F_0041abc0 &&
        rt_lookup(0x41f360)==F_0041f360 && rt_lookup(0x41eac0)==F_0041eac0 &&
        rt_lookup(0x41ac30)==F_0041ac30;
}

static void apply(CPU *c,uint32_t query,uint32_t callback,const Sh4SegmentQuery *q) {
    CPU saved=*c;
    memcpy(g_mem+query,q->ray,32);
    for(unsigned i=0;i<q->count;i++) {
        rt_guest_call(c,callback,1,q->polygon[i]);
        *c=saved;
    }
    c->eax=0;c->esp+=4;
}

void nat_collision_segments(CPU *c) {
    HOSTFN;
    if(c!=g_main_cpu) { F_004203a0_orig(c);return; }
    static int enabled=-1;
    static int mapping_ok,check_each,counters;
    static unsigned target,verified,accepted,refused,failed,empty;
    if(enabled<0) {
        const char *s=getenv("SH4_NATIVE_SEGMENTS"),*v=getenv("SH4_SEGMENT_VERIFY");
        enabled=s && !strcmp(s,"1");target=v ? (unsigned)strtoul(v,NULL,10) : 0;
        s=getenv("SH4_SEGMENT_CHECK_EACH");check_each=s && !strcmp(s,"1");
        s=getenv("SH4_SEGMENT_COUNTERS");counters=s && !strcmp(s,"1");
        mapping_ok=!enabled || mappings_match();
        if(enabled) rt_log("[segment-native] requested fcw=%x df=%u main=%d stack=%08x memory=%u\n",
            c->fcw,c->df,c==g_main_cpu,c->esp,g_mem_size);
    }
    // The game uses 0x23f; bit 6 is reserved (0x27f has the same precision,
    // rounding and exception masks). Preserve the caller's actual word.
    uint32_t control=c->fcw|0x40u;
    if(!enabled || c!=g_main_cpu || c->df || (control!=0x27f && control!=0x37f) ||
       c->esp<0x10000 || c->esp>g_mem_size-16 ||
       g_mem_size<0x13dc3f0) { F_004203a0_orig(c);return; }
    uint32_t ret=R32(c->esp),query=ARG(3),callback=R32(0xed7500);
    int nearest=ret==0x420c63 && callback==0x420a70 ? 0 :
                ret==0x420c9a && callback==0x420a90 ? 1 :
                ret==0x420cda && callback==0x420b50 ? 2 : -1;
    // An empty list does not inspect the ray, geometry, math tables or call
    // any callback in the original. Preserve that cheap no-work case.
    if(nearest>=0 && !check_each && verified>=target && (int32_t)ARG(2)<=0) {
        ++accepted;++empty;
        if(counters && accepted%100000==0)
            rt_log("[segment-count] frame=%u accepted=%u empty=%u refused=%u\n",g_frame,accepted,empty,refused);
        c->eax=0;c->esp+=4;return;
    }
    if(nearest<0 || query>g_mem_size-32 ||
       (query<c->esp+16 && (uint64_t)query+32>c->esp-0x10000) ||
       (query<0xed7a00 && (uint64_t)query+32>0xed7400) ||
       (nearest && R32(0xed793c)!=query) ||
       R32(0x13dc3dc)!=0x401ec0 || R32(0x13dc3e8)!=0x401570 || R32(0x13dc3ec)!=0x41abc0 ||
       R32(0x5ce064)!=0 || R32(0x5b729c)!=0x3f800000 ||
       R32(0x5b79cc)!=0x3727c5ac || R32(0x5b79f4)!=0xb727c5ac ||
       !(check_each ? mappings_match() : mapping_ok)) {
        if(++refused<=3) rt_log("[segment-native] frame=%u original guard caller=%08x callback=%08x math=%08x,%08x,%08x\n",
            g_frame,ret,callback,R32(0x13dc3dc),R32(0x13dc3e8),R32(0x13dc3ec));
        F_004203a0_orig(c);return;
    }
    Sh4SegmentView view={.data=g_mem,.size=g_mem_size,.polygons=ARG(1),.count=ARG(2),
        .query_address=query,.exclude_begin=c->esp-0x10000,.exclude_end=c->esp+16,.nearest=(unsigned)nearest};
    Sh4SegmentQuery q;memcpy(q.ray,g_mem+query,32);
    if(!sh4_segment_polygons(&view,&q)) {
        ++refused;F_004203a0_orig(c);return;
    }
    ++accepted;
    if(!view.count) ++empty;
    if(counters && accepted%100000==0)
        rt_log("[segment-count] frame=%u accepted=%u empty=%u refused=%u\n",g_frame,accepted,empty,refused);
    if(verified<target) {
        uint8_t before[0x600],expected[0x600],expected_ray[32];
        CPU initial=*c;memcpy(before,g_mem+0xed7400,sizeof before);
        F_004203a0_orig(c);CPU original=*c;
        memcpy(expected,g_mem+0xed7400,sizeof expected);memcpy(expected_ray,g_mem+query,32);
        memcpy(g_mem+0xed7400,before,sizeof before);*c=initial;
        apply(c,query,callback,&q);
        int bad=memcmp(g_mem+0xed7400,expected,sizeof expected) || memcmp(g_mem+query,expected_ray,32) ||
            c->eax!=original.eax || c->esp!=original.esp || c->ebx!=original.ebx ||
            c->esi!=original.esi || c->edi!=original.edi || c->ebp!=original.ebp ||
            c->ftop!=original.ftop || c->fcw!=original.fcw;
        // Keep the original state, including its volatile x87 scratch/status,
        // during verification. Audited callers discard those volatile outputs.
        *c=original;memcpy(g_mem+0xed7400,expected,sizeof expected);memcpy(g_mem+query,expected_ray,32);
        ++verified;if(bad) { ++failed;enabled=0; }
        if(verified==1 || verified%10000==0 || verified==target || bad)
            rt_log("[segment-native] frame=%u verified=%u/%u failed=%u accepted=%u refused=%u polygons=%u triangles=%u hits=%u\n",
                g_frame,verified,target,failed,accepted,refused,q.polygons,q.triangles,q.count);
        return;
    }
    apply(c,query,callback,&q);
    if(accepted==1) rt_log("[segment-native] frame=%u enabled polygons=%u triangles=%u hits=%u refused=%u\n",
        g_frame,q.polygons,q.triangles,q.count,refused);
}
