#include "rt.h"
#include "audio_adpcm.h"
#include <stdlib.h>

extern void F_0040a880_orig(CPU *c);
extern unsigned g_frame;

static int overlap(uint32_t a,uint32_t n,uint32_t b,uint32_t m) {
    return (uint64_t)a<(uint64_t)b+m && (uint64_t)b<(uint64_t)a+n;
}

// Consume a prefix of the audited 0x40ab20 packet loop. The last iteration
// remains original, reconstructing volatile registers, flags and scratch.
static int packets_enabled=-1;
static unsigned packets_target,packets_verified,packets_failed;

int rt_audio_packets_enabled(CPU *c) {
    if(packets_enabled<0) {
        const char *s=getenv("SH4_NATIVE_AUDIO_PACKETS"),*v=getenv("SH4_AUDIO_PACKETS_VERIFY");
        packets_enabled=s && !strcmp(s,"1");packets_target=v ? (unsigned)strtoul(v,NULL,10) : 0;
    }
    return packets_enabled && c==g_main_cpu;
}

uint32_t rt_adpcm_packet_prefix(CPU *c,uint32_t obj,uint32_t part,uint32_t sp,
                               uint32_t position,uint32_t written) {
    HOSTFN;
    if(!rt_audio_packets_enabled(c) || g_mem_size<0x100 || sp<0x100 || sp>g_mem_size-0x70 ||
       obj>g_mem_size-4 || (uint64_t)obj+part+0x12c0a0u>g_mem_size) return 0;
    uint32_t bytes=R32(sp+0x10),dst=R32(sp+0x68),right=R32(sp+0x14);
    uint32_t bank=R32(obj+part+0x12c09c),mode=R32(obj);
    if(bytes<52 || bytes>0x1a000 || bytes%26 || bank>1 || right!=(uint64_t)dst+2 ||
       (uint64_t)obj+part+0x12c0d0u+written!=dst) return 0;
    unsigned packets=bytes/26-1,stride=mode ? 64 : 32;
    uint64_t source=(uint64_t)obj+part+0x1ac0d0u+bank*0x96000u+position;
    uint32_t consumed=packets*26,produced=packets*stride;
    if(source>g_mem_size || bytes>g_mem_size-source || dst>g_mem_size ||
       (uint64_t)produced+stride>g_mem_size-dst || written>UINT32_MAX-produced ||
       position>UINT32_MAX-consumed || right>UINT32_MAX-produced ||
       overlap(dst,produced+stride,(uint32_t)source,bytes) ||
       overlap(dst,produced+stride,sp-0x100,0x170) ||
       overlap((uint32_t)source,bytes,sp-0x100,0x170) ||
       overlap(dst,produced+stride,obj,4) ||
       overlap(dst,produced+stride,obj+part+0x12c09c,4)) return 0;
    sh4_adpcm_packets(g_mem+dst,g_mem+(uint32_t)source,packets,mode!=0);
    if(packets_verified<packets_target) {
        // The original decoder uses only this private stack interval. Restore
        // every byte afterwards; verified PCM always comes from the original.
        uint8_t saved[0x100];memcpy(saved,g_mem+sp-0x100,sizeof saved);
        CPU shadow=*c;int bad=0;
        for(unsigned i=0;i<packets;i++) {
            uint8_t expected[64];
            for(unsigned channel=0;channel<(mode ? 2u : 1u);channel++) {
                shadow.esp=sp-0x80;
                W32(shadow.esp,channel ? 0x40ab92 : 0x40ab45);
                W32(shadow.esp+4,sp-0x40+32*channel);
                W32(shadow.esp+8,(uint32_t)source+26*i+13*channel);
                F_0040a880_orig(&shadow);
            }
            if(mode) {
                for(unsigned j=0;j<16;j++) {
                    memcpy(expected+4*j,g_mem+sp-0x40+2*j,2);
                    memcpy(expected+4*j+2,g_mem+sp-0x20+2*j,2);
                }
            } else memcpy(expected,g_mem+sp-0x40,32);
            bad|=memcmp(g_mem+dst+stride*i,expected,stride)!=0;
            memcpy(g_mem+dst+stride*i,expected,stride);
        }
        memcpy(g_mem+sp-0x100,saved,sizeof saved);
        ++packets_verified;if(bad) {++packets_failed;packets_enabled=0;}
        rt_log("[audio-packets] frame=%u verified=%u/%u failed=%u packets=%u\n",
               g_frame,packets_verified,packets_target,packets_failed,packets);
    }
    W32(sp+0x10,26);W32(sp+0x68,dst+produced);W32(sp+0x14,right+produced);
    static unsigned seen;
    if(!(seen&(mode ? 2u : 1u))) {
        seen|=mode ? 2u : 1u;
        rt_log("[audio-packets] frame=%u mode=%u packets=%u prefix=%u bytes\n",g_frame,mode,packets,consumed);
    }
    return consumed;
}

void nat_audio_adpcm(CPU *c) {
    HOSTFN;
    if(c!=g_main_cpu) {F_0040a880_orig(c);return;}
    static int enabled=-1;
    static unsigned target,verified,accepted,refused,failed,callers;
    if(enabled<0) {
        const char *s=getenv("SH4_NATIVE_ADPCM"),*v=getenv("SH4_ADPCM_VERIFY");
        enabled=s && !strcmp(s,"1");target=v ? (unsigned)strtoul(v,NULL,10) : 0;
    }
    if(!enabled || c->esp<32 || g_mem_size<32 || c->esp>g_mem_size-12) {
        F_0040a880_orig(c);return;
    }
    uint32_t ret=R32(c->esp),dst=ARG(1),src=ARG(2);
    if((ret!=0x40aada && ret!=0x40ab45 && ret!=0x40ab92) ||
       dst>g_mem_size-32 || src>g_mem_size-13 || overlap(dst,32,src,13) ||
       overlap(dst,32,c->esp-32,44) || overlap(src,13,c->esp-32,44)) {
        ++refused;F_0040a880_orig(c);return;
    }
    int checking=verified<target;
    uint8_t pcm[32];uint32_t predictor;
    uint32_t value=sh4_adpcm_block(checking ? pcm : g_mem+dst,g_mem+src,&predictor);
    ++accepted;
    unsigned bit=ret==0x40aada ? 1u : ret==0x40ab45 ? 2u : 4u;
    if(!(callers&bit)) {
        callers|=bit;
        rt_log("[adpcm-native] frame=%u first caller=%08x callers=%x\n",g_frame,ret,callers);
    }
    if(checking) {
        CPU expected=*c;
        expected.eax=src+13;expected.ecx=value;
        expected.edx=(predictor&0xffff0000u)|(value&0xffffu);
        expected.esp+=12;
        F_0040a880_orig(c);
        int bad=memcmp(g_mem+dst,pcm,sizeof pcm) || memcmp(c,&expected,sizeof expected);
        ++verified;if(bad) {++failed;enabled=0;}
        if(verified==1 || verified%10000==0 || verified==target || bad)
            rt_log("[adpcm-native] frame=%u verified=%u/%u failed=%u accepted=%u refused=%u caller=%08x\n",
                g_frame,verified,target,failed,accepted,refused,ret);
        return;
    }
    c->eax=src+13;c->ecx=value;c->edx=(predictor&0xffff0000u)|(value&0xffffu);
    c->esp+=12; // GOG ret 8: return address and both arguments
    if(accepted==1)
        rt_log("[adpcm-native] frame=%u enabled caller=%08x\n",g_frame,ret);
}
