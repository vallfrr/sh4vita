#include "collision_segment.h"
#include <math.h>
#include <string.h>

#define EXACT __attribute__((optimize("fp-contract=off")))
static uint32_t word(const uint8_t *p) { uint32_t v;memcpy(&v,p,4);return v; }
static float scalar(const uint8_t *p) { float v;memcpy(&v,p,4);return v; }
static int overlap(uint32_t a,uint32_t n,uint32_t b,uint32_t m) {
    return (uint64_t)a<(uint64_t)b+m && (uint64_t)b<(uint64_t)a+n;
}
static int span(const Sh4SegmentView *v,uint32_t p,uint32_t n) {
    return p<=v->size && n<=v->size-p && !overlap(p,n,v->query_address,32) &&
        !overlap(p,n,0xed7400,0x600) &&
        !(v->exclude_end>v->exclude_begin && overlap(p,n,v->exclude_begin,v->exclude_end-v->exclude_begin));
}
static int coordinate(uint32_t w) { return (w&0x7fffffffu)<=0x49800000u; }
static EXACT double dot(const float a[3],const float b[3]) {
    double z=(double)a[2]*b[2],y=(double)a[1]*b[1];
    return (z+y)+(double)a[0]*b[0];
}
static EXACT void cross(float r[3],const float a[3],const float b[3]) {
    r[0]=(float)((double)b[2]*a[1]-(double)a[2]*b[1]);
    r[1]=(float)((double)a[2]*b[0]-(double)b[2]*a[0]);
    r[2]=(float)((double)a[0]*b[1]-(double)b[0]*a[1]);
}

static EXACT int plane_compatible(const uint8_t *p,unsigned physical,const float ray[8],unsigned nearest) {
    if(!nearest) return 1;
    // Preserve the original reverse-winding diagnostic on the original path.
    if(!physical || (p[physical*16-3]&4)) return 0;
    float a[3],b[3],raw[3],normal[3];
    for(unsigned k=0;k<3;k++) {
        a[k]=(float)((double)scalar(p+4*k)-scalar(p+16+4*k));
        b[k]=(float)((double)scalar(p+32+4*k)-scalar(p+16+4*k));
    }
    raw[0]=(float)((double)b[1]*a[2]-(double)b[2]*a[1]);
    raw[1]=(float)((double)b[2]*a[0]-(double)a[2]*b[0]);
    raw[2]=(float)((double)b[0]*a[1]-(double)b[1]*a[0]);
    float len2=(float)(((double)raw[0]*raw[0]+(double)raw[1]*raw[1])+(double)raw[2]*raw[2]);
    float len=len2>0 ? (float)sqrt((double)len2) : 0;
    double inv=len!=0 ? 1.0/len : 0;
    for(unsigned k=0;k<3;k++) normal[k]=(float)(inv*raw[k]);
    float d=(float)((-(double)normal[0]*scalar(p)-(double)normal[1]*scalar(p+4))-
                    (double)normal[2]*scalar(p+8));
    double facing=(((double)normal[1]*ray[1]+(double)normal[2]*ray[2])+(double)normal[0]*ray[0])+d;
    double epsilon=(double)0x1.4f8b58p-17f;
    if(nearest==1 ? facing < -epsilon : facing >= -epsilon) return 1;
    float distance[2];int side[2];
    for(unsigned i=0;i<2;i++) {
        const float *v=ray+i*4;
        distance[i]=(float)((((double)normal[0]*v[0]+(double)normal[1]*v[1])+
                             (double)normal[2]*v[2])+d);
        side[i]=distance[i]<-epsilon ? -1 : distance[i]<=epsilon ? 0 : 1;
    }
    // The original callback ignores 41eac0's refusal and then reads its
    // uninitialized stack output. Changing the stack layout changes behavior.
    return side[0]!=0 && side[0]!=side[1];
}

// Same subtraction spills, z+y+x dot order, rounded reciprocal and mixed
// double/float interval comparisons as 41e6c0 -> 573a20. The interpolated
// point in 4203a0's private stack scratch is never exposed to its callbacks.
static EXACT int triangle(const float ray[8],const float direction[3],
                          const uint8_t *v0,const uint8_t *v1,const uint8_t *v2) {
    float a[3],b[3],p[3],s[3],q[3];
    for(unsigned k=0;k<3;k++) {
        a[k]=(float)((double)scalar(v1+4*k)-scalar(v0+4*k));
        b[k]=(float)((double)scalar(v2+4*k)-scalar(v0+4*k));
    }
    cross(p,direction,b);double det=dot(a,p);
    if(fabs(det)<(double)0x1.4f8b58p-17f) return 0; // GOG 0x3727c5ac
    float inverse=(float)(1.0/det);
    for(unsigned k=0;k<3;k++) s[k]=(float)((double)ray[k]-scalar(v0+4*k));
    double u=dot(s,p)*(double)inverse;float uf=(float)u;
    if(u<0 || uf>1) return 0;
    cross(q,s,a);double v=dot(direction,q)*(double)inverse;float vf=(float)v;
    if(v<0 || (double)vf+uf>1) return 0;
    double t=dot(b,q)*(double)inverse;
    return t>=0 && (float)t<=1;
}

EXACT int sh4_segment_polygons(const Sh4SegmentView *v,Sh4SegmentQuery *q) {
    if(v->count>65536 || v->nearest>2) return 0;
    for(unsigned k=0;k<8;k++) if(k!=3 && k!=7) {
        uint32_t w;memcpy(&w,q->ray+k,4);if(!coordinate(w)) return 0;
    }
    float direction[3];
    for(unsigned k=0;k<3;k++) direction[k]=(float)((double)q->ray[k+4]-q->ray[k]);
    q->count=q->polygons=q->triangles=0;
    uint32_t p=v->polygons;
    for(uint32_t i=0;i<v->count;i++) {
        if(!span(v,p,16)) return 0;
        const uint8_t *poly=v->data+p;
        unsigned physical=poly[12],n=(poly[13]&1) ? 3 : physical;
        unsigned bytes=(physical>n ? physical:n)*16;
        if(bytes && !span(v,p,bytes)) return 0;
        for(unsigned j=0;j<n;j++) for(unsigned k=0;k<3;k++)
            if(!coordinate(word(poly+j*16+k*4))) return 0;
        q->polygons++;
        if(n>=3) {
            q->ray[3]=q->ray[7]=0;
            for(unsigned j=1;j<n-1;j++) {
                q->triangles++;
                if(triangle(q->ray,direction,poly,poly+j*16,poly+(j+1)*16)) {
                    if(!plane_compatible(poly,physical,q->ray,v->nearest)) return 0;
                    if(q->count==SH4_SEGMENT_CAP) return 0;
                    q->polygon[q->count++]=p;break;
                }
            }
        }
        uint64_t next=(uint64_t)p+physical*16;
        if(next>v->size) return 0;
        p=(uint32_t)next;
    }
    return 1;
}
