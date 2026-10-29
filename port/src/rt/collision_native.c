#include "collision_native.h"
#include <math.h>
#include <string.h>

#define EXACT __attribute__((optimize("fp-contract=off")))
static uint32_t word(const uint8_t *p) { uint32_t v; memcpy(&v,p,4); return v; }
static float scalar(const uint8_t *p) { float v; memcpy(&v,p,4); return v; }
// Keep overflow/unordered intermediate behavior on the original path.
static int coordinate(const uint8_t *p) { return (word(p)&0x7fffffffu)<=0x49800000u; }
static void wf(Sh4CollisionQuery *q, unsigned offset, float f) {
    memcpy(q->work+offset/4,&f,4);
}
static int overlap(uint32_t a, uint32_t n, uint32_t b, uint32_t m) {
    return (uint64_t)a < (uint64_t)b+m && (uint64_t)b < (uint64_t)a+n;
}
static int span(const Sh4CollisionView *v, uint32_t a, uint32_t n) {
    return a <= v->size && n <= v->size-a &&
        !overlap(a,n,v->query_address,20) && !overlap(a,n,0xed7400,0x600) &&
        !(v->exclude_end > v->exclude_begin &&
          overlap(a,n,v->exclude_begin,v->exclude_end-v->exclude_begin));
}
static EXACT void normalize(float out[3], const float in[3]) {
    float len2=(float)(((double)in[0]*in[0]+(double)in[1]*in[1])+(double)in[2]*in[2]);
    float len=len2>0 ? (float)sqrt((double)len2) : 0;
    double inv=len!=0 ? 1.0/len : 0;
    for (unsigned k=0;k<3;k++) out[k]=(float)(inv*in[k]);
}
static EXACT int node_hit(const uint8_t *p,const float s[5]) {
    double r=s[4];
    if ((float)((r+scalar(p))-s[0])<0) return 0;
    if ((r+scalar(p+4))-s[1]<0) return 0;
    if ((float)((double)(float)(r+scalar(p+8))-s[2])<0) return 0;
    if ((double)s[0]-((double)scalar(p+16)-r)<0) return 0;
    if ((float)((double)s[1]-(float)((double)scalar(p+20)-r))<0) return 0;
    if ((float)((double)s[2]-(float)((double)scalar(p+24)-r))<0) return 0;
    return 1;
}

// The plane/edge stage retains the original spill boundaries; no fast-math,
// reassociation, reduced precision or change to the polygon's vertex order.
static EXACT int polygon_hit(const uint8_t *p,unsigned n,Sh4CollisionQuery *q) {
    float first[3],second[3],third[3];
    for(unsigned k=0;k<3;k++) {
        first[k]=scalar(p+4*k); second[k]=scalar(p+16+4*k); third[k]=scalar(p+32+4*k);
    }
    double a[3]; for(unsigned k=0;k<3;k++) a[k]=(double)first[k]-second[k];
    float bx=(float)((double)third[0]-second[0]);
    float by=(float)((double)third[1]-second[1]);
    double bz=(double)third[2]-second[2];
    float raw[3]={(float)((double)by*a[2]-bz*a[1]),
                  (float)(bz*a[0]-a[2]*bx),
                  (float)(a[1]*bx-(double)by*a[0])};
    float normal[3]; normalize(normal,raw);
    double plane=(-(double)normal[0]*first[0]-(double)normal[1]*first[1])-
                  (double)normal[2]*first[2];
    double dist=plane+(((double)normal[1]*q->sphere[1]+(double)normal[2]*q->sphere[2])+
                       (double)normal[0]*q->sphere[0]);
    float projected[3];
    for(unsigned k=0;k<3;k++) {
        float delta=(float)((double)normal[k]*(-dist));
        projected[k]=(float)((double)delta+q->sphere[k]);
    }
    double distance=dist<0 ? (float)(-dist) : dist;
    if (distance>q->sphere[4]) return 0;
    q->sphere[3]=1;
    float radius2=(float)((double)q->sphere[4]*q->sphere[4]-distance*distance);
    unsigned inside=0;
    for(unsigned i=0;i<n;i++) {
        const uint8_t *v=p+i*16,*next=p+((i+1)%n)*16;
        float s[3],edge[3];
        for(unsigned k=0;k<3;k++) {
            s[k]=(float)((double)projected[k]-scalar(v+k*4));
            edge[k]=(float)((double)scalar(next+k*4)-scalar(v+k*4));
        }
        double ds=((double)s[2]*s[2]+(double)s[1]*s[1])+(double)s[0]*s[0];
        float dsf=(float)ds;
        if(ds<radius2) return 1;
        double cx=(double)edge[1]*s[2]-(double)edge[2]*s[1];
        double cy=(double)edge[2]*s[0]-(double)s[2]*edge[0];
        double cz=(double)s[1]*edge[0]-(double)edge[1]*s[0];
        double side=(cz*raw[2]+cy*raw[1])+cx*raw[0];
        if(side<=-(double)(float)1e-5) {
            float len2=(float)(((double)edge[2]*edge[2]+(double)edge[1]*edge[1])+
                               (double)edge[0]*edge[0]);
            float unit[3]; normalize(unit,edge);
            double along=((double)unit[2]*s[2]+(double)unit[1]*s[1])+(double)unit[0]*s[0];
            float along2=(float)(along*along);
            if((double)dsf-along2>radius2) return 0;
            if(along>=(double)(float)1e-5 && along2<=len2) return 1;
        } else ++inside;
    }
    return inside==n;
}

static EXACT int polygons(const Sh4CollisionView *v,uint32_t poly,int32_t count,Sh4CollisionQuery *q) {
    float lo[3],hi[3];
    for(unsigned k=0;k<3;k++) {
        lo[k]=(float)((double)q->sphere[k]-q->sphere[4]);
        hi[k]=(float)((double)q->sphere[k]+q->sphere[4]);
        wf(q,0x40+k*4,lo[k]); wf(q,0x10+k*4,hi[k]);
        wf(q,0x60+k*4,hi[k]); wf(q,0x70+k*4,lo[k]);
    }
    wf(q,0x4c,q->sphere[3]); wf(q,0x1c,q->sphere[3]);
    wf(q,0x6c,q->sphere[3]); wf(q,0x7c,q->sphere[3]);
    q->work[0xa0/4]=0x420a70;
    for(int32_t i=0;i<count;i++) {
        if(++q->polygons>1000000 || !span(v,poly,48)) return 0;
        const uint8_t *p=v->data+poly;
        unsigned physical=p[12],n=(p[13]&1)?3:physical;
        if(physical<3 || n<3 || !span(v,poly,physical*16)) return 0;
        float mn[3],mx[3];
        memcpy(mn,hi,sizeof mn); memcpy(mx,lo,sizeof mx);
        q->work[0xa4/4]=0;
        for(unsigned j=0;j<n;j++) {
            const uint8_t *a=p+j*16;
            memcpy(q->work+0x90/4,a,16);
            for(unsigned k=0;k<3;k++) {
                float f=scalar(a+k*4);
                if(!coordinate(a+k*4)) return 0;
                if(f<=mn[k]) mn[k]=f;
                if(mx[k]<=f) mx[k]=f;
            }
        }
        for(unsigned k=0;k<3;k++) {
            double delta=(double)mn[k]-hi[k];
            wf(q,0x50+k*4,(float)delta);
            // x stays in double, y/z are spilled before the epsilon comparison.
            if(k) delta=(float)delta;
            if(fabs(delta)<=(double)(float)1e-5) q->work[0xa4/4]|=0x40;
            delta=(double)mx[k]-lo[k];
            wf(q,k*4,(float)delta);
            if(k) delta=(float)delta;
            if(fabs(delta)<=(double)(float)1e-5) q->work[0xa4/4]|=0x40;
        }
        if(!(q->work[0xa4/4]&0x40) && polygon_hit(p,n,q) && q->count<SH4_COLLISION_CAP)
            q->polygon[q->count++]=poly;
        poly+=physical*16;
    }
    return 1;
}

static EXACT int payload(const Sh4CollisionView *v,uint32_t node,Sh4CollisionQuery *q) {
    const uint8_t *h=v->data+node+32;
    return polygons(v,node+32+word(h+4),(int32_t)word(h),q);
}

EXACT int sh4_collision_polygons(const Sh4CollisionView *v,uint32_t poly,
                                  int32_t count,Sh4CollisionQuery *q) {
    q->nodes=q->polygons=0;
    if(q->count>SH4_COLLISION_CAP || count>1000000) return 0;
    for(unsigned k=0;k<5;k++)
        if(k==3 ? !isfinite(q->sphere[k]) : !coordinate((const uint8_t *)(q->sphere+k))) return 0;
    return polygons(v,poly,count,q);
}

EXACT int sh4_collision_sphere(const Sh4CollisionView *v,Sh4CollisionQuery *q) {
    struct Frame { uint32_t node,next; } stack[128];
    unsigned depth=0; uint32_t node=v->root;
    q->nodes=q->polygons=0;
    if(q->count>SH4_COLLISION_CAP) return 0;
    for(unsigned k=0;k<5;k++)
        if(k==3 ? !isfinite(q->sphere[k]) : !coordinate((const uint8_t *)(q->sphere+k))) return 0;
    for(;;) {
        if(++q->nodes>65536 || !span(v,node,40)) return 0;
        const uint8_t *p=v->data+node;
        for(unsigned k=0;k<3;k++)
            if(!coordinate(p+k*4) || !coordinate(p+16+k*4)) return 0;
        if(node_hit(p,q->sphere)) {
            if(!payload(v,node,q)) return 0;
            int32_t n=(int32_t)word(p+12);
            if(n>0) {
                if(n>65536 || !span(v,node+40,(uint32_t)n*4) || depth==128) return 0;
                stack[depth++]=(struct Frame){node,1};
                node+=word(p+40); continue;
            }
        }
        while(depth) {
            struct Frame *f=stack+depth-1;
            uint32_t n=word(v->data+f->node+12);
            if(f->next<n) {
                node=f->node+word(v->data+f->node+40+4*f->next++); break;
            }
            --depth;
        }
        if(!depth) return 1;
    }
}
