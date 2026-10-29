#include "audio_adpcm.h"
#include <string.h>

static uint16_t word(const uint8_t *p) {return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static inline uint32_t sample(uint32_t step,int32_t nibble,int32_t a,int32_t b,uint32_t *term) {
    *term=(uint32_t)(b*0x7298);
    uint32_t sum=step*(uint32_t)nibble-(uint32_t)(a*0x3350)+*term;
    return (sum>>14)|((sum&0x80000000u)?0xfffc0000u:0);
}

uint32_t sh4_adpcm_block(uint8_t out[32],const uint8_t in[13],uint32_t *predictor) {
    int32_t a=(int16_t)word(in),b=(int16_t)word(in+2);
    uint32_t step=(uint32_t)word(in+4)<<14,value=0,term=0;
    memcpy(out,in,4);
    for(unsigned i=0;i<7;i++) {
        uint32_t code=in[6+i];
        int32_t low=(int32_t)(code<<28)>>28,high=(int32_t)(code<<24)>>28;
        value=sample(step,low,a,b,&term);
        out[4+4*i]=(uint8_t)value;out[5+4*i]=(uint8_t)(value>>8);a=(int16_t)value;
        value=sample(step,high,b,a,&term);
        out[6+4*i]=(uint8_t)value;out[7+4*i]=(uint8_t)(value>>8);b=(int16_t)value;
    }
    *predictor=term;
    return value;
}

void sh4_adpcm_packets(uint8_t *out,const uint8_t *in,unsigned packets,int stereo) {
    while(packets--) {
        uint32_t ignored;
        if(!stereo) {
            sh4_adpcm_block(out,in,&ignored);
            out+=32;
        } else {
            uint8_t left[32],right[32];
            sh4_adpcm_block(left,in,&ignored);
            sh4_adpcm_block(right,in+13,&ignored);
            for(unsigned i=0;i<16;i++) {
                out[4*i]=left[2*i];out[4*i+1]=left[2*i+1];
                out[4*i+2]=right[2*i];out[4*i+3]=right[2*i+1];
            }
            out+=64;
        }
        in+=26;
    }
}
