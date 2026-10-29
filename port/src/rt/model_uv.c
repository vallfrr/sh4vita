#include "model_uv.h"
#include <math.h>
#include <string.h>

int sh4_model_uv_prepare(const uint8_t *vertices,uint32_t n,const Sh4ModelUvParams *p,
                         Sh4ModelUvPrepared *prepared,Sh4ModelUvStatus *status) {
    if (!isfinite(p->min_x) || !isfinite(p->min_y) || !isfinite(p->max_y) ||
        !isfinite(p->range_x) || !isfinite(p->range_y) || !isfinite(p->lo) ||
        !isfinite(p->hi) || !isfinite(p->wrap) || !isfinite(p->special)) return 0;
    int xn=p->range_x>p->lo && p->range_x<p->hi,xw=p->range_x>p->hi;
    int ym=p->range_y>p->lo && p->range_y<p->hi ? 1 : p->range_y>p->hi ? 2 : p->max_y==p->special ? 3 : 4;
    status->a=status->b=0;status->y_offset_first=ym==3;
    for (uint32_t i=0;i<n;i++,vertices+=64) {
        float uv[2];memcpy(uv,vertices+24,sizeof uv);
        if (!isfinite(uv[0]) || !isfinite(uv[1])) return 0;
        double sx=uv[0],sy=uv[1],xd=sx-p->min_x,yd=sy-p->min_y;
        double x=xd,y;
        if (xn) { x=xd*p->wrap;x=x/p->range_x; }
        else if (xw && !(xd<p->hi)) x-=p->wrap;
        if (ym==1) {
            y=yd*p->wrap;y=y/p->range_y;status->a=p->range_y;status->b=p->hi;
        } else if (ym==2) {
            y=yd;if (!(yd<p->hi)) y-=p->wrap;status->a=yd;status->b=p->hi;
        } else {
            status->a=p->max_y;status->b=p->special;y=ym==3 ? sy : yd;
        }
        prepared[i]=(Sh4ModelUvPrepared){x,y};
    }
    return 1;
}

static inline void apply(const uint8_t *vertices,uint8_t *output,uint32_t n,
                       const Sh4ModelUvPrepared *prepared,const Sh4ModelUvStatus *status,
                       double offset_x,double offset_y,int prefetch) {
    for (uint32_t i=0;i<n;i++,vertices+=64,output+=64) {
        if (prefetch && n-i>8) {
            __builtin_prefetch(vertices+8*64,0,1);
            __builtin_prefetch(prepared+i+8,0,1);
        }
        memcpy(output,vertices,64);
        uint32_t words[2];sh4_model_uv_value(&prepared[i],status,offset_x,offset_y,words);
        memcpy(output+24,words,sizeof words);
    }
}

void sh4_model_uv_apply(const uint8_t *vertices,uint8_t *output,uint32_t n,
                       const Sh4ModelUvPrepared *prepared,const Sh4ModelUvStatus *status,
                       double offset_x,double offset_y) {
    apply(vertices,output,n,prepared,status,offset_x,offset_y,0);
}
void sh4_model_uv_apply_prefetched(const uint8_t *vertices,uint8_t *output,uint32_t n,
                       const Sh4ModelUvPrepared *prepared,const Sh4ModelUvStatus *status,
                       double offset_x,double offset_y) {
    apply(vertices,output,n,prepared,status,offset_x,offset_y,1);
}

void sh4_model_uv_apply_bulk(const uint8_t *vertices,uint8_t *output,uint32_t n,
                       const Sh4ModelUvPrepared *prepared,const Sh4ModelUvStatus *status,
                       double offset_x,double offset_y) {
    // The bridge has established disjoint complete spans. Copy the current
    // channels with the platform's bulk memcpy, then update only the UV words.
    memcpy(output,vertices,(size_t)n*64);
    for (uint32_t i=0;i<n;i++,output+=64) {
        if (n-i>8) __builtin_prefetch(prepared+i+8,0,1);
        double x=prepared[i].x;x+=offset_x;
        double y;
        if (status->y_offset_first) { y=offset_y;y+=prepared[i].y; }
        else { y=prepared[i].y;y+=offset_y; }
        float xf=(float)x,yf=(float)y;
        memcpy(output+24,&xf,4);memcpy(output+28,&yf,4);
    }
}
