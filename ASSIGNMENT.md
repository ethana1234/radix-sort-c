# Multi-Core by Default: Parallel Sum & Radix Sort

## Overview

Modern CPUs have 8, 16, 32, or more cores, yet most code is written as if only
one core exists. The traditional approach — write single-core code, then
occasionally "go wide" with job systems or parallel-for constructs — introduces
complexity at every parallelization point: extra machinery to set up, scattered
control flow, and difficult debugging.

There is an alternative: **multi-core by default**. Instead of starting
single-threaded and occasionally handing work off to other cores, we start with
*all* cores running the same entry point, and occasionally **go narrow** when
only one core needs to do something (like printing output or allocating shared
memory). This is the same model GPU shaders use — and it's why GPU programming
feels so natural despite being massively parallel.

This exercise implements the multi-core-by-default paradigm in C, applying it
to two problems of increasing complexity.

## Learning Objectives

- Understand the "multi-core by default" programming model and how it contrasts
  with job systems and parallel-for patterns.
- Implement the core primitives of lane-based parallelism: lane indexing, barrier
  synchronization, work range distribution, and cross-lane data broadcasting.
- Apply uniform work distribution to parallelize an array summation.
- Implement a radix sort that distributes O(N) passes across all cores, showing
  how algorithms can be redesigned for uniform parallel work distribution.
- Appreciate that "going narrow" (masking work to one lane) is simpler than
  "going wide" from single-threaded code.

## Background

### The Model

In multi-core-by-default code, a bootstrap function launches N threads (one per
core), all executing the **same entry point**. Each thread receives its index
within the group.

```
BootstrapEntryPoint:
    for each core i in [0, N):
        launch EntryPoint(thread_idx=i)
    join all

EntryPoint(thread_idx):
    // All real work happens here, on all cores simultaneously.
```

### Key Terminology

- **Lane**: One thread within a cooperating group. Distinguished from "thread"
  because the same OS thread may participate in different groups over time.
- **Lane Index / Lane Count**: `LaneIdx()` returns this lane's index in
  `[0, LaneCount())`. These replace the raw `thread_idx` / `thread_count`.
- **Barrier Sync** (`LaneSync()`): All lanes in a group block until every lane
  has reached the barrier. Used to enforce ordering (e.g., "don't read shared
  data until the writer is done").
- **Going Narrow**: Masking work to a single lane with `if (LaneIdx() == 0)`.
  Trivially simple compared to "going wide" from single-threaded code.
- **Lane Range** (`LaneRange(count)`): Uniformly distributes `count` items
  across all lanes, returning each lane's `[first, one_past_last)` range.
  Handles remainders by giving one extra item to the first `count % LaneCount()`
  lanes.
- **Lane Broadcast** (`LaneBroadcastU64(value_ptr, src_lane)`): Copies a
  64-bit value from one lane to all others via shared storage, with barrier
  synchronization on both sides.

### Work Distribution

Given `count` items and `lane_count` lanes:

```
items_per_lane      = count / lane_count
leftover            = count % lane_count
has_leftover        = (lane_idx < leftover)
leftovers_before_me = has_leftover ? lane_idx : leftover
first               = items_per_lane * lane_idx + leftovers_before_me
one_past_last       = first + items_per_lane + (has_leftover ? 1 : 0)
```

This ensures every item is covered exactly once, with at most 1 item difference
between any two lanes.

### Radix Sort (LSD, base-256)

Radix sort sorts by examining one "digit" at a time, from least significant to
most significant. Using base-256 (one byte at a time), a 64-bit key requires
8 passes.

Each pass has three phases, all of which can be parallelized:

```
Pass for byte position `b` (b = 0..7):

  1. HISTOGRAM: Count occurrences of each byte value [0..255] in position `b`.
     Each lane histograms its own range of elements, then histograms are summed.

     Example with 12 elements, 3 lanes, looking at byte[0]:

       Index:    0   1   2   3   4   5   6   7   8   9  10  11
       Byte[0]: 03  01  03  00  02  01  01  03  02  00  01  03
               ╰─── Lane 0 ───╯╰─── Lane 1 ───╯╰─── Lane 2 ───╯

     Each lane counts byte frequencies only within its range:

                     byte: 00  01  02  03
       Lane 0 histogram:  [ 1,  1,  0,  2 ]   ← elements 0–3
       Lane 1 histogram:  [ 0,  2,  1,  1 ]   ← elements 4–7
       Lane 2 histogram:  [ 1,  1,  1,  1 ]   ← elements 8–11

     Per-lane histograms are stored in a shared 2D array
     histograms[lane_count][256] so that all lanes can access them.
     After a barrier sync, one lane (lane 0) reduces (sums) the columns:

       Global histogram:  [ 2,  4,  2,  4 ]
                            ↑    ↑
                            │    └─ byte 01 appeared 4 times total
                            └─ byte 00 appeared 2 times total

     This global histogram feeds into the prefix sum phase.

  2. PREFIX SUM: Compute exclusive prefix sums of the histogram to determine
     destination offsets. This is O(256) — negligible, done on one lane.

Histogram:
              bucket 0   bucket 1   bucket 2   bucket 3
lane 0:          3          1          0          4
lane 1:          2          2          1          1
lane 2:          1          0          3          2

Prefix Sums:
              bucket 0   bucket 1   bucket 2   bucket 3
                 0          6          9         13

Prefix Sums Per Lane:
              bucket 0   bucket 1   bucket 2   bucket 3
lane 0:          0          6          9         13
lane 1:          3          7          9         17
lane 2:          5          9         10         18

  3. SCATTER: Each element is placed at its destination in the output buffer
     based on its byte value and per-lane offset tables. Each lane scatters
     its own range using pre-computed offsets (no atomics), ensuring stability.

  After each pass, swap the source and destination buffers.
```

To handle negative numbers (signed integers), the most significant byte pass
must flip the sign-bit interpretation so that negative values sort before
positive ones. One approach: XOR the top byte with 0x80 when computing the
histogram and scatter index on the final (byte 7) pass.

```
                    ┌─────────────────────────────────┐
                    │  For each byte position b=0..7  │
                    └────────────────┬────────────────┘
                                     │
                    ┌────────────────▼────────────────┐
                    │  HISTOGRAM (parallel per lane)  │
                    │  Count byte[b] for each element │
                    │  in this lane's range           │
                    └────────────────┬────────────────┘
                                     │ LaneSync
                    ┌────────────────▼────────────────┐
                    │  REDUCE histograms (one lane    │
                    │  sums all per-lane histograms)  │
                    └────────────────┬────────────────┘
                                     │ LaneSync
                    ┌────────────────▼────────────────┐
                    │  PREFIX SUM (one lane)          │
                    │  exclusive scan of 256 counts   │
                    └────────────────┬────────────────┘
                                     │ LaneSync
                    ┌────────────────▼────────────────┐
                    │  SCATTER (parallel per lane)    │
                    │  Place elements at dest offsets │
                    │  (per-lane offsets, no atomics) │
                    └────────────────┬────────────────┘
                                     │ LaneSync
                    ┌────────────────▼────────────────┐
                    │  Swap src ↔ dst buffers         │
                    └─────────────────────────────────┘
```

## Requirements

### Milestone 1: Parallel Sum

1. Implement the lane infrastructure: `LaneIdx()`, `LaneCount()`, `LaneSync()`,
   `LaneRange(count)`, and `LaneBroadcastU64()`.
2. Implement `BootstrapEntryPoint()` which launches N threads all running the
   same entry point.
3. Implement `parallel_sum()`: compute the sum of an `int64_t` array using all
   lanes. Each lane sums its range, then results are combined (either via
   atomics or broadcast).
4. The same code must work correctly when run with `lane_count = 1`
   (single-core as a parameterization of multi-core).

### Milestone 2: Parallel Radix Sort

5. Implement `parallel_radix_sort()` for `int64_t` keys using LSD radix sort
   with base-256 (one byte per pass, 8 passes).
6. The histogram phase must be parallelized: each lane histograms its own range
   into a shared flat array `histogram[lane_count * RADIX]` (where lane `L`'s
   counts live at `histogram[L * RADIX ... (L+1) * RADIX)`), then a single lane
   reduces (sums) all per-lane histograms into a global histogram.
7. The scatter phase must be **stable** and parallelized. LSD radix sort
   requires stability — elements with the same byte value must keep their
   relative order from the previous pass. To achieve this, compute **per-lane
   offsets** after the global prefix sum: for each bucket `b`, lane 0's
   elements start at `prefix_sum[b]`, lane 1's start at
   `prefix_sum[b] + histogram[0 * RADIX + b]`, lane 2's start at
   `prefix_sum[b] + histogram[0 * RADIX + b] + histogram[1 * RADIX + b]`,
   and so on. Each lane then scatters its range using its own offsets,
   incrementing locally — no atomics needed.
8. The sort must correctly handle negative numbers (signed `int64_t`).
9. The sort must work correctly with `lane_count = 1`.

### Stretch Goals

10. Skip passes where all bytes are identical (the histogram is concentrated in
    a single bucket).
11. Benchmark and print speedup relative to single-lane execution.

## Suggested Milestones

### Milestone 1: Parallel Sum

- [ ] Get the project compiling with an empty entry point running on all cores.
- [ ] Implement `LaneRange()` and verify the ranges cover `[0, count)` exactly.
- [ ] Implement `parallel_sum()` — each lane sums its range, combine with
      atomics.
- [ ] Verify correctness against a simple single-threaded sum.
- [ ] Verify it works with `lane_count = 1`.

### Milestone 2: Parallel Radix Sort

- [ ] Implement single-lane (serial) radix sort first — get the algorithm right
      before parallelizing.
- [ ] Parallelize the histogram phase (per-lane histograms + reduction).
- [ ] Parallelize the scatter phase (per-lane offsets for stability).
- [ ] Handle signed integers on the final byte pass.
- [ ] Verify correctness against `qsort` on random data, edge cases (empty,
      single element, all same, already sorted, reverse sorted, negatives).

## Hints & References

- Wikipedia: [Radix sort](https://en.wikipedia.org/wiki/Radix_sort)
- The blog post this exercise is based on discusses the multi-core-by-default
  model in detail, including the sum example and the motivation for switching
  from comparison sort to radix sort.
- For barriers: `pthread_barrier_t` on Linux works well. Initialize with
  `pthread_barrier_init(&barrier, NULL, thread_count)`.
- For atomics: `__atomic_fetch_add(&var, val, __ATOMIC_SEQ_CST)` (GCC/Clang
  built-in) or `<stdatomic.h>` with `atomic_fetch_add()`.
- For threads: `pthread_create` / `pthread_join`.
- Think of `LaneSync()` as a "semicolon between phases" — it separates the
  program into epochs where shared data is consistent.
