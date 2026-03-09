#include "parallel_radix_sort.h"
#include "lanes.h"
#include <stdlib.h>
#include <string.h>

// Number of buckets for base-256 radix sort.
#define RADIX 256

// Number of byte passes for 64-bit keys.
#define NUM_PASSES 8

/*
 * Extracts byte `byte_idx` from a 64-bit key.
 * For the most significant byte (byte_idx == 7), XOR with 0x80 to handle
 * signed integers: this flips the sign bit so negatives sort before positives.
 */
static inline uint8_t extract_byte(int64_t key, int pass)
{
    uint8_t byte = (uint8_t)((uint64_t)key >> (pass * 8));
    if (pass == NUM_PASSES - 1) byte ^= 0x80;
    return byte;
}

/*
 * Sorts `count` signed 64-bit integers in ascending order using a parallel
 * LSD radix sort (base-256, 8 passes).
 *
 * Must be called from within a lane group. All lanes cooperate on each pass:
 *   - Histogram phase:  each lane counts byte frequencies for its range
 *   - Reduce phase:     one lane sums per-lane histograms into a global one
 *   - Prefix sum phase: one lane computes exclusive prefix sums
 *   - Scatter phase:    each lane places its elements into the output buffer
 *
 * The sort is in-place in the sense that `values` contains the sorted result
 * when the function returns. Internally, a temporary buffer of equal size is
 * allocated for double-buffering.
 *
 * Handles negative numbers by flipping the sign-bit interpretation on the
 * most significant byte pass (byte 7).
 *
 * Parameters:
 *   values  — pointer to the array to sort (same pointer on all lanes)
 *   count   — number of elements
 */
void parallel_radix_sort(int64_t *values, int64_t count)
{
    // TODO (Milestone 2):
    //
    // 0. Allocate a temporary buffer of `count` int64_t's for double-buffering.
    //    Only one lane should allocate; broadcast the pointer to all lanes.
    //    Set up src = values, dst = temp_buf.
    //
    // For each pass (byte position) b = 0..7:
    //
    //   1. HISTOGRAM
    //      - Each lane computes a local histogram[RADIX] by iterating over
    //        its range (LaneRangeOf(count)) and calling extract_byte(src[i], b).
    //      - Store per-lane histograms in a shared 2D array:
    //        histograms[LaneCount()][RADIX].
    //      - LaneSync() after writing.
    //
    //   2. REDUCE
    //      - One lane (lane 0) sums all per-lane histograms into a single
    //        global_histogram[RADIX].
    //      - LaneSync() after writing.
    //
    //   3. PREFIX SUM
    //      - One lane (lane 0) computes an exclusive prefix sum over
    //        global_histogram, turning counts into destination offsets.
    //        Store in offsets[RADIX].
    //      - LaneSync() after writing.
    //
    //   4. SCATTER
    //      - Each lane iterates over its range again.
    //      - For each element, compute the byte, then atomically increment
    //        offsets[byte] to get the destination index:
    //          int64_t dest = __atomic_fetch_add(&offsets[byte], 1,
    //                                           __ATOMIC_SEQ_CST);
    //          dst[dest] = src[i];
    //      - LaneSync() after scattering.
    //
    //   5. Swap src and dst pointers (all lanes must agree, so broadcast
    //      or just have each lane do the same swap).
    //
    // After all 8 passes:
    //   - If the final result ended up in temp_buf (not `values`), copy it
    //     back to `values` (can be parallelized with LaneRangeOf).
    //   - Free the temporary buffer (one lane).

    int64_t lane_idx = LaneIdx();
    int64_t lane_count = LaneCount();
    int64_t *temp_buf;
    int64_t *histogram;
    if (lane_idx == 0) {
        temp_buf = malloc(count * sizeof(int64_t));
        histogram = malloc(lane_count * RADIX * sizeof(int64_t));
    }
    uint64_t ptr_as_u64 = (uint64_t)temp_buf;
    LaneBroadcastU64(&ptr_as_u64, 0);
    temp_buf = (int64_t *)ptr_as_u64;
    uint64_t *hist_as_u64 = (uint64_t *)histogram;
    LaneBroadcastU64(&hist_as_u64, 0);
    histogram = (int64_t *)hist_as_u64;
    int64_t *src = values;
    int64_t *dest = temp_buf;
    for (int i=0; i<NUM_PASSES; i++) {
        // Histogram phase
        memset(&histogram[lane_idx * RADIX], 0, RADIX * sizeof(int64_t));
        LaneSync();
        LaneRange r = LaneRangeOf(count);
        for (int64_t j=r.first; j<r.one_past_last; j++) {
            uint8_t b = extract_byte(src[j], i);
            histogram[lane_idx * RADIX + b]++;
        }
        LaneSync();
        // Reduce phase
        if (lane_idx == 0) {
            for (int64_t j=1; j<lane_count; j++) {
                for (int64_t k=0; k<RADIX; k++) {
                    histogram[k] += histogram[j * RADIX + k];
                }
            }
        }
        LaneSync();
    }
}
