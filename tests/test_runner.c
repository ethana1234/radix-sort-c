#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../src/lanes.h"
#include "../src/parallel_sum.h"
#include "../src/parallel_radix_sort.h"

// ---------------------------------------------------------------------------
// Minimal test framework
// ---------------------------------------------------------------------------

static int tests_run    = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void name(void)
#define RUN_TEST(name)                                          \
    do {                                                        \
        tests_run++;                                            \
        printf("  %-50s ", #name);                              \
        name();                                                 \
    } while (0)

#define ASSERT_EQ(expected, actual)                              \
    do {                                                        \
        int64_t _e = (int64_t)(expected);                       \
        int64_t _a = (int64_t)(actual);                         \
        if (_e != _a) {                                         \
            printf("FAIL\n    %s:%d: expected %ld, got %ld\n",  \
                   __FILE__, __LINE__, _e, _a);                 \
            tests_failed++;                                     \
            return;                                             \
        }                                                       \
    } while (0)

#define ASSERT_TRUE(cond)                                       \
    do {                                                        \
        if (!(cond)) {                                          \
            printf("FAIL\n    %s:%d: assertion failed: %s\n",   \
                   __FILE__, __LINE__, #cond);                  \
            tests_failed++;                                     \
            return;                                             \
        }                                                       \
    } while (0)

#define PASS()                                                  \
    do { printf("PASS\n"); tests_passed++; } while (0)

// ---------------------------------------------------------------------------
// Helpers for running lane-based tests
// ---------------------------------------------------------------------------

// We use these globals to pass data into and out of lane entry points,
// since LaneEntryFn takes no arguments.
static const int64_t *g_test_values;
static int64_t        g_test_count;
static int64_t        g_test_result;
static int64_t       *g_test_sort_buf;

// Reference sum (single-threaded, for verification).
static int64_t reference_sum(const int64_t *values, int64_t count)
{
    int64_t sum = 0;
    for (int64_t i = 0; i < count; i++) sum += values[i];
    return sum;
}

// Reference sort (qsort, for verification).
static int cmp_int64(const void *a, const void *b)
{
    int64_t va = *(const int64_t *)a;
    int64_t vb = *(const int64_t *)b;
    return (va > vb) - (va < vb);
}

static void reference_sort(int64_t *values, int64_t count)
{
    qsort(values, (size_t)count, sizeof(int64_t), cmp_int64);
}

// Fill array with random int64_t values.
static void fill_random(int64_t *values, int64_t count)
{
    for (int64_t i = 0; i < count; i++) {
        // Combine two rand() calls for wider range.
        values[i] = ((int64_t)rand() << 32) ^ ((int64_t)rand());
    }
}

// =========================================================================
// Milestone 1: Lane infrastructure tests
// =========================================================================

// --- LaneRange tests (these don't need threads, just set g_lane_ctx) ---

TEST(test_lane_range_single_lane)
{
    g_lane_ctx.lane_idx = 0;
    g_lane_ctx.lane_count = 1;

    LaneRange r = LaneRangeOf(100);
    ASSERT_EQ(0, r.first);
    ASSERT_EQ(100, r.one_past_last);
    PASS();
}

TEST(test_lane_range_even_split)
{
    // 4 lanes, 100 items -> 25 each
    int64_t total = 0;
    for (int64_t i = 0; i < 4; i++) {
        g_lane_ctx.lane_idx = i;
        g_lane_ctx.lane_count = 4;
        LaneRange r = LaneRangeOf(100);
        ASSERT_EQ(25, r.one_past_last - r.first);
        total += r.one_past_last - r.first;
    }
    ASSERT_EQ(100, total);
    PASS();
}

TEST(test_lane_range_uneven_split)
{
    // 4 lanes, 10 items -> 3,3,2,2 (first 2 lanes get an extra)
    int64_t total = 0;
    int64_t prev_opl = 0;
    for (int64_t i = 0; i < 4; i++) {
        g_lane_ctx.lane_idx = i;
        g_lane_ctx.lane_count = 4;
        LaneRange r = LaneRangeOf(10);
        ASSERT_EQ(prev_opl, r.first);  // ranges are contiguous
        total += r.one_past_last - r.first;
        prev_opl = r.one_past_last;
    }
    ASSERT_EQ(10, total);
    ASSERT_EQ(10, prev_opl);
    PASS();
}

TEST(test_lane_range_more_lanes_than_items)
{
    // 8 lanes, 3 items -> first 3 lanes get 1 each, rest get 0
    int64_t total = 0;
    for (int64_t i = 0; i < 8; i++) {
        g_lane_ctx.lane_idx = i;
        g_lane_ctx.lane_count = 8;
        LaneRange r = LaneRangeOf(3);
        int64_t len = r.one_past_last - r.first;
        ASSERT_TRUE(len == 0 || len == 1);
        total += len;
    }
    ASSERT_EQ(3, total);
    PASS();
}

TEST(test_lane_range_zero_items)
{
    g_lane_ctx.lane_idx = 0;
    g_lane_ctx.lane_count = 4;
    LaneRange r = LaneRangeOf(0);
    ASSERT_EQ(0, r.first);
    ASSERT_EQ(0, r.one_past_last);
    PASS();
}

// --- Bootstrap / LaneIdx / LaneCount tests ---

static void entry_check_lane_count(void)
{
    // Each lane writes its index to verify correct setup.
    // We abuse g_test_sort_buf to collect indices.
    int64_t idx = LaneIdx();
    int64_t cnt = LaneCount();
    g_test_sort_buf[idx] = cnt;
}

TEST(test_bootstrap_and_lane_identity)
{
    int64_t lane_count = 4;
    int64_t buf[8] = {0};
    g_test_sort_buf = buf;

    BootstrapLanes(lane_count, entry_check_lane_count);

    for (int64_t i = 0; i < lane_count; i++) {
        ASSERT_EQ(lane_count, buf[i]);
    }
    PASS();
}

TEST(test_bootstrap_single_lane)
{
    int64_t buf[1] = {0};
    g_test_sort_buf = buf;

    BootstrapLanes(1, entry_check_lane_count);
    ASSERT_EQ(1, buf[0]);
    PASS();
}

// =========================================================================
// Milestone 1: Parallel sum tests
// =========================================================================

static void entry_sum(void)
{
    g_test_result = parallel_sum(g_test_values, g_test_count);
}

TEST(test_sum_basic)
{
    int64_t values[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    g_test_values = values;
    g_test_count  = 10;
    g_test_result = 0;

    BootstrapLanes(4, entry_sum);
    ASSERT_EQ(55, g_test_result);
    PASS();
}

TEST(test_sum_single_lane)
{
    int64_t values[] = {10, 20, 30};
    g_test_values = values;
    g_test_count  = 3;
    g_test_result = 0;

    BootstrapLanes(1, entry_sum);
    ASSERT_EQ(60, g_test_result);
    PASS();
}

TEST(test_sum_empty)
{
    g_test_values = NULL;
    g_test_count  = 0;
    g_test_result = -1;

    BootstrapLanes(4, entry_sum);
    ASSERT_EQ(0, g_test_result);
    PASS();
}

TEST(test_sum_single_element)
{
    int64_t values[] = {42};
    g_test_values = values;
    g_test_count  = 1;
    g_test_result = 0;

    BootstrapLanes(4, entry_sum);
    ASSERT_EQ(42, g_test_result);
    PASS();
}

TEST(test_sum_negatives)
{
    int64_t values[] = {-5, -4, 10, -2, 0};
    g_test_values = values;
    g_test_count  = 5;
    g_test_result =0;

    BootstrapLanes(4, entry_sum);
    ASSERT_EQ(-1, g_test_result);
    PASS();
}

TEST(test_sum_large)
{
    int64_t count = 100000;
    int64_t *values = malloc((size_t)count * sizeof(int64_t));
    for (int64_t i = 0; i < count; i++) values[i] = i + 1;

    g_test_values = values;
    g_test_count  = count;
    g_test_result = 0;

    BootstrapLanes(8, entry_sum);
    int64_t expected = (count * (count + 1)) / 2;
    ASSERT_EQ(expected, g_test_result);

    free(values);
    PASS();
}

// =========================================================================
// Milestone 2: Parallel radix sort tests
// =========================================================================

static void entry_sort(void)
{
    parallel_radix_sort(g_test_sort_buf, g_test_count);
}

static void run_sort_test(int64_t *values, int64_t count, int64_t lane_count)
{
    // Make a copy for reference sort.
    int64_t *expected = malloc((size_t)count * sizeof(int64_t));
    memcpy(expected, values, (size_t)count * sizeof(int64_t));
    reference_sort(expected, count);

    g_test_sort_buf = values;
    g_test_count    = count;

    BootstrapLanes(lane_count, entry_sort);

    for (int64_t i = 0; i < count; i++) {
        if (values[i] != expected[i]) {
            printf("FAIL\n    index %ld: expected %ld, got %ld\n",
                   i, expected[i], values[i]);
            tests_failed++;
            free(expected);
            return;
        }
    }
    free(expected);
    printf("PASS\n");
    tests_passed++;
}

TEST(test_sort_basic)
{
    int64_t values[] = {5, 3, 8, 1, 9, 2, 7, 4, 6, 0};
    run_sort_test(values, 10, 4);
}

TEST(test_sort_single_lane)
{
    int64_t values[] = {9, 7, 5, 3, 1, 0, 2, 4, 6, 8};
    run_sort_test(values, 10, 1);
}

TEST(test_sort_empty)
{
    g_test_sort_buf = NULL;
    g_test_count    = 0;
    BootstrapLanes(4, entry_sort);
    PASS();
}

TEST(test_sort_single_element)
{
    int64_t values[] = {42};
    run_sort_test(values, 1, 4);
}

TEST(test_sort_already_sorted)
{
    int64_t values[] = {1, 2, 3, 4, 5};
    run_sort_test(values, 5, 4);
}

TEST(test_sort_reverse)
{
    int64_t values[] = {5, 4, 3, 2, 1};
    run_sort_test(values, 5, 4);
}

TEST(test_sort_all_same)
{
    int64_t values[] = {7, 7, 7, 7, 7};
    run_sort_test(values, 5, 4);
}

TEST(test_sort_negatives)
{
    int64_t values[] = {-3, -1, -4, -1, -5, -9, -2, -6};
    run_sort_test(values, 8, 4);
}

TEST(test_sort_mixed_sign)
{
    int64_t values[] = {3, -1, 4, -1, 5, -9, 2, -6, 0};
    run_sort_test(values, 9, 4);
}

TEST(test_sort_large_random)
{
    int64_t count = 50000;
    int64_t *values = malloc((size_t)count * sizeof(int64_t));
    fill_random(values, count);
    tests_run++;
    printf("  %-50s ", "test_sort_large_random");
    run_sort_test(values, count, 8);
    tests_run--; // run_sort_test already incremented via the macro-less path
    free(values);
}

TEST(test_sort_boundary_values)
{
    int64_t values[] = {INT64_MAX, INT64_MIN, 0, -1, 1, INT64_MAX, INT64_MIN};
    run_sort_test(values, 7, 4);
}

// =========================================================================
// Main
// =========================================================================

static const char *test_filter = NULL;

#define RUN_TEST_FILTERED(name)                                     \
    do {                                                            \
        if (test_filter && !strstr(#name, test_filter)) break;      \
        RUN_TEST(name);                                             \
    } while (0)

int main(int argc, char *argv[])
{
    srand((unsigned)time(NULL));

    if (argc > 1) {
        test_filter = argv[1];
        printf("\nFilter: \"%s\"\n", test_filter);
    }

    printf("\n=== Milestone 1: Lane Infrastructure ===\n");
    RUN_TEST_FILTERED(test_lane_range_single_lane);
    RUN_TEST_FILTERED(test_lane_range_even_split);
    RUN_TEST_FILTERED(test_lane_range_uneven_split);
    RUN_TEST_FILTERED(test_lane_range_more_lanes_than_items);
    RUN_TEST_FILTERED(test_lane_range_zero_items);
    RUN_TEST_FILTERED(test_bootstrap_and_lane_identity);
    RUN_TEST_FILTERED(test_bootstrap_single_lane);

    printf("\n=== Milestone 1: Parallel Sum ===\n");
    RUN_TEST_FILTERED(test_sum_basic);
    RUN_TEST_FILTERED(test_sum_single_lane);
    RUN_TEST_FILTERED(test_sum_empty);
    RUN_TEST_FILTERED(test_sum_single_element);
    RUN_TEST_FILTERED(test_sum_negatives);
    RUN_TEST_FILTERED(test_sum_large);

    printf("\n=== Milestone 2: Parallel Radix Sort ===\n");
    RUN_TEST_FILTERED(test_sort_basic);
    RUN_TEST_FILTERED(test_sort_single_lane);
    RUN_TEST_FILTERED(test_sort_empty);
    RUN_TEST_FILTERED(test_sort_single_element);
    RUN_TEST_FILTERED(test_sort_already_sorted);
    RUN_TEST_FILTERED(test_sort_reverse);
    RUN_TEST_FILTERED(test_sort_all_same);
    RUN_TEST_FILTERED(test_sort_negatives);
    RUN_TEST_FILTERED(test_sort_mixed_sign);
    RUN_TEST_FILTERED(test_sort_large_random);
    RUN_TEST_FILTERED(test_sort_boundary_values);

    printf("\n-------------------------------------------\n");
    printf("Results: %d passed, %d failed, %d total\n\n",
           tests_passed, tests_failed, tests_run);

    return tests_failed > 0 ? 1 : 0;
}
