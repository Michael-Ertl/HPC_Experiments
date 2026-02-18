#include <benchmark/benchmark.h>
#include "utils.h"

static void Basic(benchmark::State& state) {
    int n = state.range(0);
    for (auto _ : state) {
        for (int i = 0; i < n; i++) {
            for (int j = 0; j < 5; j++) {
                benchmark::DoNotOptimize(i + j);
            }
        }
    }
    state.SetItemsProcessed(5 * n);
    state.counters["n"] = n;
}

BENCHMARK(Basic)->RangeMultiplier(2)->Range(4, 64)->Unit(benchmark::kMillisecond);
