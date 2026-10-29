// Transactional segment/polygon candidate filtering over bounded GOG resources.
#pragma once
#include <stdint.h>

#define SH4_SEGMENT_CAP 256
typedef struct {
    const uint8_t *data;
    uint32_t size,polygons,count,query_address,exclude_begin,exclude_end;
    unsigned nearest; // 0 ordered collector; 1 front-face callback, 2 back-face
} Sh4SegmentView;
typedef struct {
    float ray[8];
    uint32_t count,polygon[SH4_SEGMENT_CAP];
    unsigned polygons,triangles;
} Sh4SegmentQuery;

// No CPU, guest writes, callbacks, allocations or persistent pointer cache.
// Discard the output on refusal. Successful hits retain polygon/fan order;
// the bridge still executes the original audited callbacks on these hits.
int sh4_segment_polygons(const Sh4SegmentView *view,Sh4SegmentQuery *query);
