#include <random>

#include "utils.h"
#include "./Solvers/StochasticLocalSearch.h"
#include "./problem_instance.h"
#include <spdlog/spdlog.h>


int main() {
    spdlog::flush_on(spdlog::level::trace);
    spdlog::set_level(spdlog::level::trace);
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] %^[%l]%$ [%s:%# %!] %v");

    LOG_INFO("Program startup.");

    std::vector<ProblemInstance> instances =
        readAllInstances("./benchmark_instances");

    for (const auto& instance : instances) {
        stochasticLocalSearch(instance, 10000, true);
    }
    ProblemInstance instance = readInstance("./benchmark_instances/c101.txt");
    stochasticLocalSearch(instance, 10000, true);
    return 0;
}
