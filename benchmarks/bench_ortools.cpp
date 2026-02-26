#include <benchmark/benchmark.h>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "ortools/constraint_solver/routing.h"
#include "ortools/constraint_solver/routing_enums.pb.h"
#include "ortools/constraint_solver/routing_index_manager.h"
#include "ortools/constraint_solver/routing_parameters.h"
#include "problem_instance.h"

using operations_research::Assignment;
using operations_research::DefaultRoutingSearchParameters;
using operations_research::RoutingDimension;
using operations_research::RoutingIndexManager;
using operations_research::RoutingModel;
using operations_research::RoutingSearchParameters;

#ifndef BENCHMARK_INSTANCES_DIR
#define BENCHMARK_INSTANCES_DIR "benchmark_instances"
#endif

// Euclidean distance scaled to integer (OR-Tools requires integer callbacks).
// Multiply by 100 to preserve two decimal places of precision.
static int64_t Distance(const ProblemInstance& inst, int from, int to) {
    const auto& a = inst.customers[from];
    const auto& b = inst.customers[to];
    double dx = a.x - b.x;
    double dy = a.y - b.y;
    return static_cast<int64_t>(std::round(std::sqrt(dx * dx + dy * dy) * 100.0));
}

struct SolveResult {
    bool solved;
    int64_t totalDistance; // scaled x100
    int vehiclesUsed;
};

static SolveResult SolveVRPTW(const ProblemInstance& inst) {
    const int n = static_cast<int>(inst.customers.size()); // index 0 is depot
    const int numVehicles = static_cast<int>(inst.numberOfVehicles);
    const RoutingIndexManager::NodeIndex depot{0};

    RoutingIndexManager manager(n, numVehicles, depot);
    RoutingModel routing(manager);

    // Arc cost: Euclidean distance (scaled x100)
    const int transitIdx = routing.RegisterTransitCallback(
        [&inst, &manager](int64_t from, int64_t to) -> int64_t {
            return Distance(inst, manager.IndexToNode(from).value(),
                                  manager.IndexToNode(to).value());
        });
    routing.SetArcCostEvaluatorOfAllVehicles(transitIdx);

    // Capacity dimension
    const int demandIdx = routing.RegisterUnaryTransitCallback(
        [&inst, &manager](int64_t idx) -> int64_t {
            return inst.customers[manager.IndexToNode(idx).value()].demand;
        });
    routing.AddDimension(demandIdx, /*slack=*/0,
                         static_cast<int64_t>(inst.capacityPerVehicle),
                         /*fix_start_cumul_to_zero=*/true, "Capacity");

    // Time dimension.
    // CumulVar(node) = time vehicle arrives at / starts service at that node.
    // Transit from i to j = service_time[i] + travel_time(i, j), both scaled x100.
    const int timeIdx = routing.RegisterTransitCallback(
        [&inst, &manager](int64_t from, int64_t to) -> int64_t {
            int f = manager.IndexToNode(from).value();
            int t = manager.IndexToNode(to).value();
            return Distance(inst, f, t) +
                   static_cast<int64_t>(inst.customers[f].serviceTime) * 100;
        });

    // Horizon: latest possible time across all nodes (scaled x100).
    int64_t horizon = 0;
    for (const auto& c : inst.customers)
        horizon = std::max(horizon, static_cast<int64_t>(c.latestLeaveTime) * 100);

    // slack = horizon allows waiting at any node up to the planning horizon.
    routing.AddDimension(timeIdx, /*slack=*/horizon, horizon,
                         /*fix_start_cumul_to_zero=*/false, "Time");
    const RoutingDimension& timeDim = routing.GetDimensionOrDie("Time");

    // Customer time windows (index 0 = depot, rest = customers).
    for (int i = 0; i < n; ++i) {
        int64_t nodeIdx = manager.NodeToIndex(RoutingIndexManager::NodeIndex(i));
        timeDim.CumulVar(nodeIdx)->SetRange(
            static_cast<int64_t>(inst.customers[i].earliestArrivalTime) * 100,
            static_cast<int64_t>(inst.customers[i].latestLeaveTime) * 100);
    }
    // Depot time windows for vehicle start/end nodes.
    for (int v = 0; v < numVehicles; ++v) {
        timeDim.CumulVar(routing.Start(v))->SetRange(
            static_cast<int64_t>(inst.customers[0].earliestArrivalTime) * 100,
            static_cast<int64_t>(inst.customers[0].latestLeaveTime) * 100);
        timeDim.CumulVar(routing.End(v))->SetRange(0, horizon);
        routing.AddVariableMinimizedByFinalizer(timeDim.CumulVar(routing.Start(v)));
        routing.AddVariableMinimizedByFinalizer(timeDim.CumulVar(routing.End(v)));
    }

    RoutingSearchParameters params = DefaultRoutingSearchParameters();
    // PARALLEL_CHEAPEST_INSERTION handles tight VRPTW time windows well.
    params.set_first_solution_strategy(
        operations_research::FirstSolutionStrategy::PARALLEL_CHEAPEST_INSERTION);
    params.set_local_search_metaheuristic(
        operations_research::LocalSearchMetaheuristic::GUIDED_LOCAL_SEARCH);
    params.mutable_time_limit()->set_seconds(10);

    const Assignment* solution = routing.SolveWithParameters(params);
    if (!solution) return {false, 0, 0};

    int64_t totalDist = 0;
    int vehiclesUsed = 0;
    for (int v = 0; v < numVehicles; ++v) {
        if (!routing.IsVehicleUsed(*solution, v)) continue;
        ++vehiclesUsed;
        int64_t idx = routing.Start(v);
        while (!routing.IsEnd(idx)) {
            int64_t next = solution->Value(routing.NextVar(idx));
            totalDist += Distance(inst,
                manager.IndexToNode(idx).value(),
                manager.IndexToNode(next).value());
            idx = next;
        }
    }
    return {true, totalDist, vehiclesUsed};
}

static std::vector<ProblemInstance>& Instances() {
    static std::vector<ProblemInstance> instances =
        readAllInstances(BENCHMARK_INSTANCES_DIR);
    return instances;
}

static void BM_OrtoolsVRPTW(benchmark::State& state) {
    const ProblemInstance& inst = Instances()[state.range(0)];

    SolveResult result{};
    for (auto _ : state) {
        result = SolveVRPTW(inst);
    }

    state.counters["customers"] = static_cast<double>(inst.customers.size() - 1);
    state.counters["vehicles_used"] = result.vehiclesUsed;
    state.counters["total_dist_x100"] = static_cast<double>(result.totalDistance);
    state.counters["solved"] = result.solved ? 1.0 : 0.0;
}

// Register one benchmark per instance at static-init time, before
// benchmark::Initialize() / benchmark_main runs.
static const bool kRegistered = []() {
    auto& instances = Instances();
    for (int i = 0; i < static_cast<int>(instances.size()); ++i) {
        benchmark::RegisterBenchmark(
                ("BM_OrtoolsVRPTW/" + instances[i].name).c_str(),
                BM_OrtoolsVRPTW)
            ->Arg(i)
            ->Unit(benchmark::kMillisecond)
            ->Iterations(1);
    }
    return true;
}();
