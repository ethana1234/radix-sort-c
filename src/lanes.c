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
    return g_lane_ctx.lane_idx;
}

/*
 * Returns the number of lanes in the current group.
 */
int64_t LaneCount(void)
{
    return g_lane_ctx.lane_count;
}

/*
 * Barrier: blocks until all lanes in the group have called LaneSync().
 */
void LaneSync(void)
{
    if (g_lane_ctx.lane_count == 1) return;
    // This function returns -1 for one thread and 0 for the rest
    // This could be a useful alternative for "going narrow"
    pthread_barrier_wait(g_lane_ctx.barrier);
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
    int64_t items_per_lane = count / g_lane_ctx.lane_count;
    int64_t leftover = count % g_lane_ctx.lane_count;
    int64_t has_leftover = g_lane_ctx.lane_idx < leftover;
    int64_t leftovers_before_me = has_leftover ? g_lane_ctx.lane_idx : leftover;
    r.first = items_per_lane * g_lane_ctx.lane_idx + leftovers_before_me;
    r.one_past_last = r.first + items_per_lane + (has_leftover ? 1 : 0);
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
    if (g_lane_ctx.lane_idx == src_lane_idx) *g_lane_ctx.shared_buf = *value_ptr;
    LaneSync();
    if (g_lane_ctx.lane_idx != src_lane_idx) *value_ptr = *g_lane_ctx.shared_buf;
    LaneSync();
}

// ---------------------------------------------------------------------------
// Bootstrap
// ---------------------------------------------------------------------------

static void *thread_start(void *raw_args)
{
    ThreadStartArgs *thread_start_args = (ThreadStartArgs *)raw_args;
    g_lane_ctx.lane_idx = thread_start_args->lane_idx;
    g_lane_ctx.lane_count = thread_start_args->lane_count;
    g_lane_ctx.barrier = thread_start_args->barrier;
    g_lane_ctx.shared_buf = thread_start_args->shared_buf;
    thread_start_args->entry_fn();
    return NULL;
}

/*
 * Launches `lane_count` threads, each executing `entry_fn`. Blocks until all
 * threads have finished. If lane_count is 1, may run entry_fn directly on the
 * calling thread (no OS thread needed).
 */
void BootstrapLanes(int64_t lane_count, LaneEntryFn entry_fn)
{
    if (lane_count == 1) {
        g_lane_ctx.lane_idx = 0;
        g_lane_ctx.lane_count = 1;
        g_lane_ctx.shared_buf = malloc(sizeof(uint64_t));
        entry_fn();
        free(g_lane_ctx.shared_buf);
        return;
    }
    pthread_barrier_t *barrier = malloc(sizeof(pthread_barrier_t));
    pthread_barrier_init(barrier, NULL, lane_count);
    uint64_t *shared_buf = malloc(sizeof(uint64_t));
    pthread_t *thread_ids = malloc(lane_count * sizeof(pthread_t));
    ThreadStartArgs **thread_args_array = malloc(lane_count * sizeof(ThreadStartArgs *));
    for (int64_t i=0; i<lane_count; i++) {
        thread_args_array[i] = malloc(sizeof(ThreadStartArgs));
        thread_args_array[i]->lane_idx = i;
        thread_args_array[i]->lane_count = lane_count;
        thread_args_array[i]->barrier = barrier;
        thread_args_array[i]->shared_buf = shared_buf;
        thread_args_array[i]->entry_fn = entry_fn;
        pthread_create(&(thread_ids[i]), NULL, thread_start, thread_args_array[i]);
    }
    for (int64_t i=0; i<lane_count; i++) pthread_join(thread_ids[i], NULL);
    free(barrier);
    free(shared_buf);
    free(thread_ids);
    free(thread_args_array);
}
