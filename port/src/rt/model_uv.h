// CPU-independent preparation of immutable UV coordinates. Positions and the
// remaining vertex channels are copied from the current vertex stream at use.
#pragma once
#include <stdint.h>
#include <string.h>
typedef struct {
    double min_x,min_y,max_y,range_x,range_y,lo,hi,wrap,special;
} Sh4ModelUvParams;
typedef struct { double x,y; } Sh4ModelUvPrepared;
typedef struct { double a,b; int y_offset_first; } Sh4ModelUvStatus;

static inline void sh4_model_uv_value(const Sh4ModelUvPrepared *prepared,const Sh4ModelUvStatus *status,
                                     double offset_x,double offset_y,uint32_t words[2]) {
    double x=prepared->x;x+=offset_x;
    double y;
    if (status->y_offset_first) { y=offset_y;y+=prepared->y; }
    else { y=prepared->y;y+=offset_y; }
    float uv[2]={(float)x,(float)y};memcpy(words,uv,sizeof uv);
}

// Caller supplies valid disjoint spans for n 64-byte vertices and n outputs.
// Refuses nonfinite source/parameters; the original path remains available.
// No prepared result may be used when this returns zero.
int sh4_model_uv_prepare(const uint8_t *vertices,uint32_t n,const Sh4ModelUvParams *params,
                         Sh4ModelUvPrepared *prepared,Sh4ModelUvStatus *status);
void sh4_model_uv_apply(const uint8_t *vertices,uint8_t *output,uint32_t n,
                       const Sh4ModelUvPrepared *prepared,const Sh4ModelUvStatus *status,
                       double offset_x,double offset_y);
void sh4_model_uv_apply_prefetched(const uint8_t *vertices,uint8_t *output,uint32_t n,
                       const Sh4ModelUvPrepared *prepared,const Sh4ModelUvStatus *status,
                       double offset_x,double offset_y);
void sh4_model_uv_apply_bulk(const uint8_t *vertices,uint8_t *output,uint32_t n,
                       const Sh4ModelUvPrepared *prepared,const Sh4ModelUvStatus *status,
                       double offset_x,double offset_y);
