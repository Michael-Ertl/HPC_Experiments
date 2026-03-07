#include <benchmark/benchmark.h>
#include <vector>
#include <string>
#include "../src/utils.h"
#include "../src/problem_instance.h"
#include "../src/Solvers/StochasticLocalSearch.h"

static std::vector<ProblemInstance> instances;

static void loadInstancesOnce() {
    if (!instances.empty()) return;
    instances = readAllInstances("./benchmark_instances");
    // Keep only the first three instances if more exist
    if (instances.size() > 3) {
        instances.resize(3);
    }
}

static void BM_SLS(benchmark::State& state) {
    // Correct way: retrieve values from the .Args() call
    const int instanceIdx = state.range(0);
    const int iterations  = state.range(1);
    
    // Ensure instances are loaded in each thread/process
    loadInstancesOnce();
    const ProblemInstance& instance = instances.at(instanceIdx);
    // Set the name as a string label
    double last_cost = 0;
    for (auto _ : state) {
        OptimizationStats stats = stochasticLocalSearch(instance, iterations);
        last_cost = stats.finalScore;
        benchmark::DoNotOptimize(last_cost);
    }

    state.counters["SearchIters"] = iterations;
    state.counters["SolutionCost"] = benchmark::Counter(last_cost, benchmark::Counter::kAvgThreads);
}

// registration function
static int RegisterAllMyBenchmarks() {
    loadInstancesOnce();
    
    std::vector<int> iterationSweep = {10, 100, 1000, 10000, 100000};

    for (size_t i = 0; i < instances.size(); ++i) {
        for (int iters : iterationSweep) {
            // Unique name for every row
            std::string benchName = instances[i].name;
            
            // Fix: Remove the (int)i from the RegisterBenchmark call itself
            // Pass it through ->Args instead
            benchmark::RegisterBenchmark(benchName.c_str(), BM_SLS)
                ->Args({(int)i, iters}) 
                ->Unit(benchmark::kMillisecond);
        }
    }
    return 0; 
}

// Dot-based static trigger
static int dummy = RegisterAllMyBenchmarks();

BENCHMARK_MAIN();