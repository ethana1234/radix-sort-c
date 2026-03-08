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
    return 0;
}
