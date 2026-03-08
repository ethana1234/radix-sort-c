#include "lanes.h"
#include <stdlib.h>
#include <string.h>

// Thread-local lane context — each thread gets its own copy.
_Thread_local LaneCtx g_lane_ctx;

// ---------------------------------------------------------------------------
// Lane API
// ---------------------------------------------------------------------------

/*
 * Returns this lane's index in [0, LaneCount()).
 */
int64_t LaneIdx(void)
{
    // TODO: Return this lane's index from g_lane_ctx.
    return -1;
}

/*
 * Returns the number of lanes in the current group.
 */
int64_t LaneCount(void)
{
    // TODO: Return the lane count from g_lane_ctx.
    return -1;
}

/*
 * Barrier: blocks until all lanes in the group have called LaneSync().
 */
void LaneSync(void)
{
    // TODO: If lane_count == 1, return immediately (no sync needed).
    // Otherwise, use pthread_barrier_wait on g_lane_ctx.barrier.
}

/*
 * Uniformly distributes `count` items across all lanes and returns the
 * calling lane's range. Handles remainders so that the first
 * (count % LaneCount()) lanes each get one extra item.
 */
LaneRange LaneRangeOf(int64_t count)
{
    // TODO: Uniformly distribute `count` items across LaneCount() lanes.
    // Return the [first, one_past_last) range for this lane (LaneIdx()).
    //
    // Algorithm:
    //   items_per_lane      = count / lane_count
    //   leftover            = count % lane_count
    //   has_leftover        = (lane_idx < leftover)
    //   leftovers_before_me = has_leftover ? lane_idx : leftover
    //   first               = items_per_lane * lane_idx + leftovers_before_me
    //   one_past_last       = first + items_per_lane + (has_leftover ? 1 : 0)
    //
    LaneRange r;
    r.first = 0;
    r.one_past_last = 0;
    return r;
}

/*
 * Broadcasts a uint64_t from `src_lane_idx` to all other lanes.
 * After this call returns, *value_ptr holds the same value on every lane.
 * Internally calls LaneSync() twice (before and after the copy).
 */
void LaneBroadcastU64(uint64_t *value_ptr, int64_t src_lane_idx)
{
    // TODO: Broadcast a single uint64_t from the source lane to all lanes.
    //
    // Steps:
    //   1. If this lane is the source, copy *value_ptr into *shared_buf.
    //   2. LaneSync() — ensure the write is visible.
    //   3. If this lane is NOT the source, copy *shared_buf into *value_ptr.
    //   4. LaneSync() — ensure all lanes have read before buffer is reused.
    //
}

// ---------------------------------------------------------------------------
// Bootstrap
// ---------------------------------------------------------------------------

static void *thread_start(void *raw_args)
{
    // TODO: Cast raw_args to ThreadStartArgs*, populate g_lane_ctx from it,
    // then call the entry function.
    return NULL;
}

/*
 * Launches `lane_count` threads, each executing `entry_fn`. Blocks until all
 * threads have finished. If lane_count is 1, may run entry_fn directly on the
 * calling thread (no OS thread needed).
 */
void BootstrapLanes(int64_t lane_count, LaneEntryFn entry_fn)
{
    // TODO:
    // 1. If lane_count == 1, set up g_lane_ctx directly, call entry_fn(),
    //    and return (no threads needed).
    //
    // 2. Otherwise:
    //    a. Allocate and init a pthread_barrier_t for `lane_count` threads.
    //    b. Allocate a shared_buf (uint64_t*) for cross-lane communication.
    //    c. For each lane, allocate a ThreadStartArgs, fill it in, and
    //       call pthread_create with thread_start as the start routine.
    //    d. pthread_join all threads.
    //    e. Free the barrier, shared_buf, and thread args.
}
