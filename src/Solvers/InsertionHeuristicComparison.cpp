//
// INSERTION HEURISTIC COMPARISON
// Compares different initialization/insertion heuristics for VRPTW
//

#include "../problem_instance.h"
#include "../allocators.h"
#include "../array.h"
#include "../utils.h"

#include <algorithm>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <chrono>
#include <cmath>
#include <limits>
#include <memory>
#include <random>
#include <vector>
#include <spdlog/spdlog.h>
#include <iostream>
#include <fstream>
#include <sstream>

struct Context {
    const ProblemInstance &instance;
    float *distanceMatrix;
    bool *arcMatrix;

    Context(const ProblemInstance &instance) : instance(instance) {
        size_t numCustomers = instance.customers.size();

        distanceMatrix = new float[numCustomers * numCustomers];
        for (size_t i = 0; i < numCustomers; i++) {
            for (size_t j = 0; j < numCustomers; j++) {
                const Customer &a = instance.customers[i];
                const Customer &b = instance.customers[j];
                double dx = a.x - b.x;
                double dy = a.y - b.y;
                distanceMatrix[i * numCustomers + j] = (float)std::sqrt(dx * dx + dy * dy);
            }
        }

        arcMatrix = new bool[numCustomers * numCustomers];
        for (size_t i = 0; i < numCustomers; i++) {
            for (size_t j = 0; j < numCustomers; j++) {
                if (i == j) continue;

                size_t idx = i * numCustomers + j;
                arcMatrix[idx] = instance.customers[i].earliestArrivalTime + distanceMatrix[idx] <= instance.customers[j].latestLeaveTime;
            }
        }
    }

    float dist(size_t i, size_t j) const {
        return distanceMatrix[i * instance.customers.size() + j];
    }

    bool feasibleArc(size_t i, size_t j) const {
        return arcMatrix[i * instance.customers.size() + j];
    }

    ~Context() {
        delete[] distanceMatrix;
        delete[] arcMatrix;
    }
};

using namespace Allocator;
using StdAllocator = Instrument<ElectricFence<Fallback<Allocator::Freelist<Allocator::Contiguous<4096 * 4>, 0, 256, Allocator::NoStorage>, Allocator::Malloc>>>;

struct Route {
    DynamicArray<StdAllocator, u16> customers;
    double lastCost;
};

struct Solution {
    DynamicArray<StdAllocator, Route> routes;
    double lastCost;
};

// returns -1 on infeasible instance
// returns total route distance/cost otherwise
template<bool CHANGE_ROUTE_COST = false>
static double evaluateRouteCost(Context& context, Route& route) {
    INSTRUMENT_SCOPE("evaluateRouteCost");

    constexpr size_t depotIdx = 0;

    int load = 0;
    float cost = 0;
    size_t previousVisitIdx = depotIdx;

    for(int customerIdx : route.customers) {

        const Customer& customer = context.instance.customers[customerIdx];
        load += customer.demand;
        if(load > context.instance.capacityPerVehicle) {
            return -1;
        }

        float travelDistance = context.dist(previousVisitIdx, customerIdx);
        float arrival = cost + travelDistance;

        if (arrival > customer.latestLeaveTime) {
            return -1;
        }

        double waitingTime = 0;
        if (customer.earliestArrivalTime > arrival) {
            waitingTime += customer.earliestArrivalTime - arrival;
        }

        cost += travelDistance + waitingTime + context.instance.customers[customerIdx].serviceTime;
        previousVisitIdx = customerIdx;
    }

    cost += context.dist(previousVisitIdx, depotIdx);

    if constexpr (CHANGE_ROUTE_COST) {
        route.lastCost = cost;
    }

    return cost;
}

static double evaluateSolution(
        Context& context,
        Solution &s) {
    double total = 0;

    for (Route &route : s.routes) {
        double routeCost = evaluateRouteCost<true>(context, route);
        if (routeCost == -1) {
            return -1;
        }
        total += routeCost;
    }
    s.lastCost = total;
    return total;
}

// ------------------------------------------------------------
// SWEEP INITIALIZATION
// ------------------------------------------------------------
Solution sweepInitialization(
        StdAllocator &alloc,
        Context& context) {
    Solution s{
            .routes = DynamicArray<StdAllocator, Route>(alloc, 8)
    };

    struct AngleCustomer {
        int id;
        double angle;
    };

    std::vector<AngleCustomer> customers;

    const Customer& depot = context.instance.customers[0];

    for (size_t i = 1; i < context.instance.customers.size(); ++i) {
        const Customer& c = context.instance.customers[i];
        double angle = atan2(c.y - depot.y, c.x - depot.x);
        customers.push_back({(int)i, angle});
    }

    std::sort(customers.begin(), customers.end(),
               [](auto &a, auto &b) { return a.angle < b.angle; });

    Route current{
            .customers = DynamicArray<StdAllocator, u16>(alloc, 8)
    };

    for (auto &c : customers) {
        current.customers.pushBack(c.id);

        if (evaluateRouteCost(context, current) == -1) {
            current.customers.popBack();

            s.routes.pushBack(std::move(current));

            current = {
                    .customers = DynamicArray<StdAllocator, u16>(alloc, 8)
            };

            current.customers.pushBack(c.id);
        }
    }

    if (!current.customers.empty())
        s.routes.pushBack(std::move(current));

    return s;
}

// ------------------------------------------------------------
// SOLOMON INSERTION INITIALIZATION
// ------------------------------------------------------------
Solution solomonInsertionInitialization(
        StdAllocator &alloc,
        Context& context) {
    Solution s{
            .routes = DynamicArray<StdAllocator, Route>(alloc, 8)
    };

    std::vector<int> unrouted;

    for (size_t i = 1; i < context.instance.customers.size(); ++i)
        unrouted.push_back(i);

    while (!unrouted.empty()) {
        Route r{
                .customers = DynamicArray<StdAllocator, u16>(alloc, 8)
        };

        r.customers.pushBack(unrouted.back());
        unrouted.pop_back();

        bool improved = true;

        while (improved && !unrouted.empty()) {
            improved = false;

            double bestCost = std::numeric_limits<double>::max();
            int bestCustomer = -1;
            int bestPos = -1;

            for (int c : unrouted) {
                DynamicArray<StdAllocator, u16> temp(alloc, 1);
                temp.pushBack((u16)c);
                
                for (size_t pos = 0; pos <= r.customers.size(); ++pos) {
                    r.customers.insertRangeAt(pos, temp, 0, 1);

                    double cost = evaluateRouteCost(context, r);

                    if (cost != -1 && cost < bestCost) {
                        bestCost = cost;
                        bestCustomer = c;
                        bestPos = pos;
                    }

                    r.customers.eraseRange(pos, pos + 1);
                }
            }

            if (bestCustomer != -1) {
                DynamicArray<StdAllocator, u16> toInsert(alloc, 1);
                toInsert.pushBack((u16)bestCustomer);
                r.customers.insertRangeAt(bestPos, toInsert, 0, 1);

                unrouted.erase(
                        std::remove(unrouted.begin(), unrouted.end(), bestCustomer),
                        unrouted.end());

                improved = true;
            }
        }

        s.routes.pushBack(std::move(r));
    }

    return s;
}

// ------------------------------------------------------------
// GREEDY MIN VEHICLES INITIALIZATION
// ------------------------------------------------------------
static Solution greedyMinVehiclesInit(StdAllocator &alloc, Context& context) {
    Solution s = Solution{
            .routes = DynamicArray<StdAllocator, Route>(alloc, 8)
    };

    DynamicArray<StdAllocator, u16> customers(alloc, context.instance.customers.size() - 1);
    for (u16 i = 1; i < (u16)context.instance.customers.size(); ++i) {
        customers.pushBack(i);
    }

    std::sort(customers.begin(), customers.end(),
        [&](int a, int b) {
            const auto& A = context.instance.customers[a];
            const auto& B = context.instance.customers[b];
            if (A.demand != B.demand) return A.demand > B.demand;
            return A.latestLeaveTime < B.latestLeaveTime;
        });

    for (int c : customers) {
        bool placed = false;

        for (Route& r : s.routes) {
            r.customers.pushBack(c);
            if (evaluateRouteCost(context, r) != -1) {
                placed = true;
                break;
            }
            r.customers.popBack();
        }

        if (!placed) {
            Route r = {
                    .customers = DynamicArray<StdAllocator, u16>(alloc, 8),
                    .lastCost = 0.0
            };
            r.customers.pushBack(c);
            s.routes.pushBack(std::move(r));
        }
    }

    return s;
}

// ------------------------------------------------------------
// NEAREST NEIGHBOR INITIALIZATION
// ------------------------------------------------------------
Solution nearestNeighborInitialization(
        StdAllocator &alloc,
        Context& context) {
    Solution s{
            .routes = DynamicArray<StdAllocator, Route>(alloc, 8)
    };

    std::vector<int> unrouted;
    for (size_t i = 1; i < context.instance.customers.size(); ++i)
        unrouted.push_back(i);

    while (!unrouted.empty()) {
        Route r{
                .customers = DynamicArray<StdAllocator, u16>(alloc, 8)
        };

        int depotIdx = 0;
        int lastCustomer = depotIdx;

        while (!unrouted.empty()) {
            double bestDist = std::numeric_limits<double>::max();
            int bestCustomer = -1;

            for (int c : unrouted) {
                float d = context.dist(lastCustomer, c);
                if (d < bestDist) {
                    bestDist = d;
                    bestCustomer = c;
                }
            }

            if (bestCustomer == -1) break;

            r.customers.pushBack(bestCustomer);

            bool feasible = evaluateRouteCost(context, r) != -1;
            if (!feasible) {
                r.customers.popBack();
                break;
            }

            lastCustomer = bestCustomer;
            unrouted.erase(std::remove(unrouted.begin(), unrouted.end(), bestCustomer), unrouted.end());
        }

        if (!r.customers.empty())
            s.routes.pushBack(std::move(r));
        else
            break;
    }

    return s;
}

// ------------------------------------------------------------
// K-MEANS CLUSTERING INITIALIZATION
// ------------------------------------------------------------
static Solution kmeansInitialization(StdAllocator &alloc, Context& context) {
    const size_t numCustomers = context.instance.customers.size();
    const size_t numDepot = 1;
    const size_t numCustomersNonDepot = numCustomers - numDepot;

    if (numCustomersNonDepot == 0) {
        Solution s{.routes = DynamicArray<StdAllocator, Route>(alloc, 8)};
        return s;
    }

    size_t k = std::min((size_t)5, numCustomersNonDepot);
    std::vector<double> centroidsX(k, 0.0);
    std::vector<double> centroidsY(k, 0.0);
    std::vector<std::vector<int>> clusters(k);
    std::vector<int> assignments(numCustomersNonDepot, 0);

    std::mt19937 rng(42);
    std::uniform_int_distribution<size_t> dist(1, numCustomers - 1);
    for (size_t i = 0; i < k; ++i) {
        size_t idx = dist(rng);
        centroidsX[i] = context.instance.customers[idx].x;
        centroidsY[i] = context.instance.customers[idx].y;
    }

    bool changed = true;
    int maxIterations = 50;
    int iter = 0;

    while (changed && iter < maxIterations) {
        changed = false;
        ++iter;

        for (auto& c : clusters) c.clear();

        for (size_t i = 0; i < numCustomersNonDepot; ++i) {
            const Customer& cust = context.instance.customers[i + 1];
            double bestDist = std::numeric_limits<double>::max();
            size_t bestCluster = 0;

            for (size_t j = 0; j < k; ++j) {
                double dx = cust.x - centroidsX[j];
                double dy = cust.y - centroidsY[j];
                double d = dx * dx + dy * dy;
                if (d < bestDist) {
                    bestDist = d;
                    bestCluster = j;
                }
            }

            if (assignments[i] != (int)bestCluster) {
                assignments[i] = (int)bestCluster;
                changed = true;
            }
            clusters[bestCluster].push_back((int)(i + 1));
        }

        std::vector<double> newCentroidsX(k, 0.0);
        std::vector<double> newCentroidsY(k, 0.0);
        std::vector<size_t> counts(k, 0);

        for (size_t j = 0; j < k; ++j) {
            for (int custIdx : clusters[j]) {
                const Customer& c = context.instance.customers[custIdx];
                newCentroidsX[j] += c.x;
                newCentroidsY[j] += c.y;
                counts[j]++;
            }
            if (counts[j] > 0) {
                newCentroidsX[j] /= counts[j];
                newCentroidsY[j] /= counts[j];
            } else {
                newCentroidsX[j] = centroidsX[j];
                newCentroidsY[j] = centroidsY[j];
            }
        }

        centroidsX = newCentroidsX;
        centroidsY = newCentroidsY;
    }

    Solution s{.routes = DynamicArray<StdAllocator, Route>(alloc, 8)};

    for (size_t j = 0; j < k; ++j) {
        if (clusters[j].empty()) continue;

        Route r{.customers = DynamicArray<StdAllocator, u16>(alloc, 8)};

        std::vector<int> cluster = clusters[j];
        std::sort(cluster.begin(), cluster.end(), [&](int a, int b) {
            const Customer& A = context.instance.customers[a];
            const Customer& B = context.instance.customers[b];
            if (A.earliestArrivalTime != B.earliestArrivalTime)
                return A.earliestArrivalTime < B.earliestArrivalTime;
            return A.latestLeaveTime < B.latestLeaveTime;
        });

        for (int c : cluster) {
            r.customers.pushBack(c);
            if (evaluateRouteCost(context, r) == -1) {
                r.customers.popBack();
                s.routes.pushBack(std::move(r));
                r = {.customers = DynamicArray<StdAllocator, u16>(alloc, 8)};
                r.customers.pushBack(c);
            }
        }

        if (!r.customers.empty())
            s.routes.pushBack(std::move(r));
    }

    std::vector<int> unclustered;
    for (size_t i = 0; i < numCustomersNonDepot; ++i) {
        bool found = false;
        for (const auto& cl : clusters) {
            for (int idx : cl) {
                if ((int)(i + 1) == idx) {
                    found = true;
                    break;
                }
            }
            if (found) break;
        }
        if (!found) unclustered.push_back((int)(i + 1));
    }

    for (int c : unclustered) {
        bool placed = false;
        for (Route& rt : s.routes) {
            rt.customers.pushBack(c);
            if (evaluateRouteCost(context, rt) != -1) {
                placed = true;
                break;
            }
            rt.customers.popBack();
        }
        if (!placed) {
            Route newR{.customers = DynamicArray<StdAllocator, u16>(alloc, 8)};
            newR.customers.pushBack(c);
            s.routes.pushBack(std::move(newR));
        }
    }

    return s;
}

struct InitResult {
    std::string name;
    double cost;
    size_t numRoutes;
    bool feasible;
};

int main() {
    spdlog::flush_on(spdlog::level::info);
    spdlog::set_level(spdlog::level::info);
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] %^[%l]%$ %v");

    LOG_INFO("Loading instances...");
    std::vector<ProblemInstance> instances = readAllInstances("./benchmark_instances");
    LOG_INFO("Loaded {} instances", instances.size());

    std::vector<InitResult> results;

    for (const auto& instance : instances) {
        LOG_INFO("Processing instance: {}", instance.name);

        Context context(instance);

        using A1 = Freelist<Contiguous<4096 * 4>, 0, 256, Allocator::NoStorage>;
        using A2 = Malloc;
        A1 a1;
        A2 a2;
        Fallback<A1, A2> fallback = Fallback(a1, a2);
        auto eFence = ElectricFence<decltype(fallback)>(fallback);
        StdAllocator alloc = Instrument<decltype(eFence)>(eFence);

        std::vector<std::pair<std::string, Solution(*)(StdAllocator&, Context&)>> initFuncs = {
            {"Sweep", sweepInitialization},
            {"Solomon", solomonInsertionInitialization},
            {"GreedyMinVehicles", greedyMinVehiclesInit},
            {"NearestNeighbor", nearestNeighborInitialization},
            {"KMeans", kmeansInitialization}
        };

        for (auto& [name, func] : initFuncs) {
            Solution sol = func(alloc, context);
            double cost = evaluateSolution(context, sol);
            bool feasible = cost != -1;

            results.push_back({instance.name + "_" + name, feasible ? cost : -1, sol.routes.size(), feasible});

            LOG_INFO("  {}: cost={}, routes={}, feasible={}", name, 
                     feasible ? std::to_string(cost) : "infeasible", 
                     sol.routes.size(), feasible);
        }
    }

    LOG_INFO("\n=== SUMMARY ===");
    LOG_INFO("{:<40} {:>12} {:>10} {:>10}", "Instance_Method", "Cost", "Routes", "Feasible");
    LOG_INFO("{}", std::string(72, '-'));

    for (const auto& r : results) {
        LOG_INFO("{:<40} {:>12.2f} {:>10} {:>10}", r.name, r.cost, r.numRoutes, r.feasible ? "Yes" : "No");
    }

    std::ofstream csvFile("insertion_heuristic_comparison.csv");
    csvFile << "Instance,Method,Cost,Routes,Feasible\n";
    for (const auto& r : results) {
        size_t underscorePos = r.name.rfind('_');
        std::string instanceName = r.name.substr(0, underscorePos);
        std::string methodName = r.name.substr(underscorePos + 1);
        csvFile << instanceName << "," << methodName << "," << r.cost << "," << r.numRoutes << "," << (r.feasible ? 1 : 0) << "\n";
    }
    csvFile.close();
    LOG_INFO("\nResults saved to insertion_heuristic_comparison.csv");

    return 0;
}
