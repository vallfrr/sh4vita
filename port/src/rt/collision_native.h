// Native sphere candidate service over a bounded view of the original GOG data.
// No CPU/register state or persistent pointer cache belongs to this interface.
#pragma once
#include <stdint.h>

#define SH4_COLLISION_CAP 256
#define SH4_COLLISION_WORK_WORDS 42
typedef struct {
    const uint8_t *data;
    uint32_t size, root, query_address;
    uint32_t exclude_begin, exclude_end;
} Sh4CollisionView;
typedef struct {
    float sphere[5];
    uint32_t count, polygon[SH4_COLLISION_CAP];
    // Compatibility state for consumers which still use the guest workspace.
    uint32_t work[SH4_COLLISION_WORK_WORDS];
    unsigned nodes, polygons;
} Sh4CollisionQuery;

// Transactional: on refusal the caller must discard the output, with no writes
// made through the read-only scene view. No allocation or cross-query caching.
int sh4_collision_sphere(const Sh4CollisionView *view, Sh4CollisionQuery *query);

// Same candidate service for a packed polygon list without a BSP node.
// A nonpositive count still updates the compatibility workspace, as GOG does.
int sh4_collision_polygons(const Sh4CollisionView *view, uint32_t polygons,
                           int32_t count, Sh4CollisionQuery *query);
