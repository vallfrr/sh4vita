// DXT1 -> existing dithered RGB565, without an intermediate RGBA8 image.
// This preserves the port's current DXT1 RGB policy (including palette 3).
#ifndef SH4_DXT16_H
#define SH4_DXT16_H
#include <stdint.h>
// Restrict the optional compressed backend to whole power-of-two mip chains
// supported by its block swizzle. Thin/NPOT textures keep all levels on the
// existing software path instead of mixing GPU formats within one object.
static inline int sh4_dxt_hardware_eligible(uint32_t w,uint32_t h,unsigned skip) {
    if(!w || !h || (w&(w-1)) || (h&(h-1)) || skip>=32)return 0;
    uint32_t bw=w>>skip,bh=h>>skip;
    return bw>=4 && bh>=4 && bw<=2048 && bh<=2048;
}
static inline void sh4_dxt1_rgb565(const uint8_t *src,uint16_t *dst,uint32_t w,uint32_t h) {
    static const uint8_t bayer[16]={0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5};
    for(uint32_t by=0;by<(h+3)/4;by++) for(uint32_t bx=0;bx<(w+3)/4;bx++,src+=8) {
        unsigned c0=src[0]|src[1]<<8,c1=src[2]|src[3]<<8;
        uint32_t bits=(uint32_t)src[4]|(uint32_t)src[5]<<8|(uint32_t)src[6]<<16|(uint32_t)src[7]<<24;
        unsigned p[4][3];
        unsigned colors[2]={c0,c1};
        for(unsigned k=0;k<2;k++) {
            p[k][0]=((colors[k]>>11)&31)*255/31;
            p[k][1]=((colors[k]>>5)&63)*255/63;
            p[k][2]=(colors[k]&31)*255/31;
        }
        for(unsigned k=0;k<3;k++) {
            if(c0>c1) {p[2][k]=(2*p[0][k]+p[1][k])/3;p[3][k]=(p[0][k]+2*p[1][k])/3;}
            else {p[2][k]=(p[0][k]+p[1][k])/2;p[3][k]=0;}
        }
        for(unsigned y=0;y<4;y++) for(unsigned x=0;x<4;x++) {
            unsigned px=bx*4+x,py=by*4+y;if(px>=w||py>=h)continue;
            const unsigned *c=p[(bits>>(2*(y*4+x)))&3];unsigned d=bayer[y*4+x];
            unsigned r=c[0]+(d>>1),g=c[1]+(d>>2),b=c[2]+(d>>1);
            if(r>255)r=255;
            if(g>255)g=255;
            if(b>255)b=255;
            dst[py*w+px]=(uint16_t)((r>>3)<<11|(g>>2)<<5|b>>3);
        }
    }
}
#endif
