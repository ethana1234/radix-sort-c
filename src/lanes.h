#ifndef LANES_H
#define LANES_H

#include <stdint.h>
#include <pthread.h>

// ---------------------------------------------------------------------------
// Lane Context
// ---------------------------------------------------------------------------
// Each thread in a lane group has its own LaneCtx, stored in thread-local
// storage. It holds the thread's index within the group, the total lane count,
// a pointer to the shared barrier, and a pointer to a small shared buffer used
// for broadcasting values between lanes.

typedef struct {
    int64_t  lane_idx;
    int64_t  lane_count;
    pthread_barrier_t *barrier;
    // Shared buffer — same pointer for all lanes in a group.
    // Used by LaneBroadcastU64, atomic accumulation, and other cross-lane ops.
    uint64_t *shared_buf;
} LaneCtx;

// Thread-local lane context. Set up by BootstrapEntryPoint before calling
// the user's entry point.
extern _Thread_local LaneCtx g_lane_ctx;

// ---------------------------------------------------------------------------
// Lane API — you implement these in lanes.c
// ---------------------------------------------------------------------------

int64_t LaneIdx(void);

int64_t LaneCount(void);

void LaneSync(void);

// A half-open range [first, one_past_last).
typedef struct {
    int64_t first;
    int64_t one_past_last;
} LaneRange;

LaneRange LaneRangeOf(int64_t count);

void LaneBroadcastU64(uint64_t *value_ptr, int64_t src_lane_idx);

// ---------------------------------------------------------------------------
// Bootstrap — you implement this in lanes.c
// ---------------------------------------------------------------------------

// The user provides a function with this signature as the multi-core entry
// point. It receives no arguments — all context comes from the lane API
// (LaneIdx, LaneCount, etc.) and from whatever shared data the lanes set up.
typedef void (*LaneEntryFn)(void);

// Internal: passed to pthread_create. Holds per-thread setup data.
typedef struct {
    int64_t            lane_idx;
    int64_t            lane_count;
    pthread_barrier_t *barrier;
    uint64_t          *shared_buf;
    LaneEntryFn        entry_fn;
} ThreadStartArgs;

void BootstrapLanes(int64_t lane_count, LaneEntryFn entry_fn);

#endif // LANES_H
