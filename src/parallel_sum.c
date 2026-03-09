#include "parallel_sum.h"
#include "lanes.h"

int64_t parallel_sum(const int64_t *values, int64_t count)
{
    // TODO (Milestone 1):
    //
    // 1. Use LaneRangeOf(count) to get this lane's portion of the array.
    //
    // 2. Sum the elements in [range.first, range.one_past_last).
    //
    // 3. Combine per-lane sums into a single total. Two approaches:
    //    (a) Atomic add: have a shared sum variable (via broadcast pointer)
    //        and use __atomic_fetch_add to accumulate each lane's result.
    //    (b) Broadcast: store each lane's sum into a shared array, sync,
    //        then have every lane compute the total.
    //    Either works — pick whichever you prefer.
    //
    // 4. Return the total sum (same value on every lane).
    int64_t lane_idx = LaneIdx();
    // Use lane 0 to init total sum to 0
    if (!lane_idx) *g_lane_ctx.shared_buf = 0;
    LaneSync();
    LaneRange r = LaneRangeOf(count);
    int64_t partial_sum = 0;
    for (int64_t i=r.first; i<r.one_past_last; i++) partial_sum += values[i];
    //printf("adding %ld to %ld on lane %ld\n", *g_lane_ctx.shared_buf, partial_sum, lane_idx);
    atomic_fetch_add((_Atomic uint64_t *)g_lane_ctx.shared_buf, partial_sum);
    LaneSync();
    return *g_lane_ctx.shared_buf;
}
