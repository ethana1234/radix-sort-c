#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "lanes.h"
#include "parallel_sum.h"
#include "parallel_radix_sort.h"

#define ARRAY_SIZE 100000
#define LANE_COUNT 4

static int64_t g_values[ARRAY_SIZE];
static int64_t g_sum_result;

static void sum_entry(void)
{
    g_sum_result = parallel_sum(g_values, ARRAY_SIZE);
}

static void sort_entry(void)
{
    parallel_radix_sort(g_values, ARRAY_SIZE);
}

int main(void)
{
    srand((unsigned)time(NULL));

    // Fill with random values.
    for (int64_t i = 0; i < ARRAY_SIZE; i++) {
        g_values[i] = (int64_t)(rand() % 10000) - 5000;
        // Uncomment for larger values that fill 64 bit ints
        //g_values[i] = ((int64_t)rand() << 32) ^ ((int64_t)rand());
    }

    printf("Multi-Core by Default — Parallel Sum & Radix Sort\n");
    printf("==================================================\n\n");
    printf("Array size:  %d\n", ARRAY_SIZE);
    printf("Lane count:  %d\n", LANE_COUNT);
    printf("\n");

    // --- Parallel Sum ---
    printf("Computing parallel sum...\n");
    BootstrapLanes(LANE_COUNT, sum_entry);
    printf("Sum: %ld\n\n", g_sum_result);

    // --- Parallel Radix Sort ---
    printf("Running parallel radix sort...\n");
    //BootstrapLanes(LANE_COUNT, sort_entry);

    // Verify sorted.
    int sorted = 1;
    for (int64_t i = 1; i < ARRAY_SIZE; i++) {
        if (g_values[i] < g_values[i - 1]) {
            sorted = 0;
            break;
        }
    }
    printf("Sorted: %s\n", sorted ? "yes" : "NO — something is wrong!");

    return 0;
}
