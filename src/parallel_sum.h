#ifndef PARALLEL_SUM_H
#define PARALLEL_SUM_H

#include <stdint.h>
#include <stdatomic.h>

// Computes the sum of `count` values using all available lanes.
// Must be called from within a lane group (i.e., from a function invoked
// via BootstrapLanes).
//
// Each lane sums its portion of the array (determined by LaneRangeOf),
// then the per-lane results are combined so that every lane has the total.
//
// Parameters:
//   values  — pointer to the array (same pointer on all lanes)
//   count   — number of elements in the array
//
// Returns:
//   The total sum of all elements. The return value is the same on every lane.
int64_t parallel_sum(const int64_t *values, int64_t count);

#endif // PARALLEL_SUM_H
