#include <benchmark/benchmark.h>
#include "solver.h"

// Basic timing benchmark
static void BM_SolverBasic(benchmark::State& state) {
    int n = state.range(0);
    auto problem = make_problem(n);
    Solver s;

    for (auto _ : state) {
        auto result = s.solve(problem);
        benchmark::DoNotOptimize(result); // prevent dead-code elimination
    }

    // Custom HPC metrics — these show up in the output table
    state.SetItemsProcessed(state.iterations() * n);
    state.counters["n"] = n;
}

// Sweep over problem sizes
BENCHMARK(BM_SolverBasic)->RangeMultiplier(2)->Range(64, 1024)->Unit(benchmark::kMillisecond);

// Comparing two approaches on the same axes
static void BM_SolverV2(benchmark::State& state) {
    int n = state.range(0);
    auto problem = make_problem(n);
    SolverV2 s;

    for (auto _ : state) {
        auto result = s.solve(problem);
        benchmark::DoNotOptimize(result);
    }

    state.counters["n"] = n;
    state.counters["residual"] = benchmark::Counter(last_residual); // quality axis
    state.counters["GFLOPS"] = benchmark::Counter(
        flop_count, benchmark::Counter::kIsRate, benchmark::Counter::kIs1000);
}

BENCHMARK(BM_SolverV2)->RangeMultiplier(2)->Range(64, 1024)->Unit(benchmark::kMillisecond);
