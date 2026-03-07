//
// SIMPLE STOCHASTIC LOCAL SEARCH VRPTW
//

#include "./StochasticLocalSearch.h"
#include "../allocators.h"
#include "../array.h"
#include "../utils.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <new>
#include <random>
#include <type_traits>
#include <spdlog/spdlog.h>
#include <iostream>

using namespace Allocator;
using StdAllocator = Instrument<ElectricFence<Fallback<Allocator::Freelist<Allocator::Contiguous<4096 * 4>, 0, 256, Allocator::NoStorage>, Allocator::Malloc>>>;

using TmpAllocator = Allocator::Freelist<Allocator::Contiguous<4096 * 4>, 0, 256, Allocator::NoStorage>; // ElectricFence<Fallback<Allocator::Freelist<Allocator::Contiguous<4096 * 4>, 0, 256, Allocator::NoStorage>, Allocator::Malloc>>;

struct Route {
    DynamicArray<StdAllocator, u16> customers;
    bool changed;
    double lastCost;

};


struct Solution {
    DynamicArray<StdAllocator, Route> routes;
};


#include <algorithm>
#include <algorithm>

bool operator==(const Route& a, const Route& b)
{
	if (a.customers.size() != b.customers.size())
		return false;

	return std::equal(
		a.customers.begin(),
		a.customers.end(),
		b.customers.begin()
	);
}

bool operator==(const Solution& a, const Solution& b)
{
	if (a.routes.size() != b.routes.size())
		return false;

	return std::equal(
		a.routes.begin(),
		a.routes.end(),
		b.routes.begin()
	);
}



size_t hashSolution(const Solution& s)
{
	size_t h = 0;

	for (const Route& r : s.routes)
	{
		for (auto c : r.customers)
		{
			h = h * 31 + c;
		}

		h = h * 131; // route separator
	}

	return h;
}

#include <unordered_set>

struct TabuList {
	std::unordered_set<size_t> hashes;

	bool contains(const Solution& s) const {
		return hashes.contains(hashSolution(s));
	}

	void push(const Solution& s) {
		hashes.insert(hashSolution(s));
	}
};


// returns -1 on infeasible instance
// returns total route distance/cost otherwise
static double evaluateRouteCost(
        const ProblemInstance& instance,
		float *dist_mat,
        const Route& route){
	INSTRUMENT_SCOPE("evaluateRouteCost");

	int depotIdx = 0;
	int load = 0;
	double time = 0;
	double distanceSum = 0;
	int previousVisitIdx = depotIdx;

	for(int customerIdx : route.customers) {

		const Customer& customer = instance.customers[customerIdx];

		load += customer.demand;

		if(load > instance.capacityPerVehicle)
		    return -1; // this instance is infeasible, since a single vehicle cannot serve a customer

		double travelDistance = dist_mat[previousVisitIdx * instance.customers.size() + customerIdx];

		double arrival=time+travelDistance; // time == eunclidean distance in solomon benchmarks

		if(arrival>customer.latestLeaveTime)
			return -1; // We don't reach this customer fast enough -> instance is infeasible

		double waitingTime = 0;
		if (customer.earliestArrivalTime>arrival) {
			waitingTime += customer.earliestArrivalTime - arrival; // we might have to wait until the custome rcan be served
		}

		distanceSum += travelDistance + waitingTime;
		previousVisitIdx = customerIdx;
	}

	distanceSum += dist_mat[previousVisitIdx * instance.customers.size() + depotIdx]; // return to depot
	return distanceSum;
}

// returns -1 on infeasible route
// returns total fleetCost otherwise
static double evaluateSolution(
        const ProblemInstance& ins,
		float *dist_mat,
        const Solution &s){
    INSTRUMENT_SCOPE("evaluateSolution");

    int64_t total=0;
    int routesEvaluated = 0;
    int routesCached = 0;

    for(Route &route : s.routes) {
        double routeCost = 0;
        if (route.changed) {
            routesEvaluated++;
            {
                INSTRUMENT_SCOPE("evaluateRouteCost_call");
                routeCost = evaluateRouteCost(ins, dist_mat, route);
            }
            if(routeCost == -1){ // route infeasible
                return -1;
            }

            route.changed = false;
            route.lastCost = routeCost;
        } else {
            routesCached++;
            routeCost = route.lastCost;
        }
        total += routeCost;
    }

    TRACY_PLOT("routes_evaluated", routesEvaluated);
    TRACY_PLOT("routes_cached", routesCached);
    return total;
}



// --- Greedy init: use as few vehicles as possible (cost quality is irrelevant) ---
static Solution greedyMinVehiclesInit(StdAllocator &alloc, const ProblemInstance& ins, float *dist_mat)
{
    INSTRUMENT_SCOPE("greedyMinVehiclesInit");
    Solution s = Solution{
	    .routes = DynamicArray<StdAllocator, Route>(alloc, 8)
    };

    // customers are 1..n (0 is depot)
    DynamicArray<StdAllocator, u16> customers(alloc, ins.customers.size() - 1);
    for (u16 i = 1; i < (u16)ins.customers.size(); ++i) {
        customers.pushBack(i);
    }

    // Heuristic ordering biased toward fewer vehicles:
    // pack big demands early; break ties by tighter deadlines.
    std::sort(customers.begin(), customers.end(),
        [&](int a, int b){
            const auto& A = ins.customers[a];
            const auto& B = ins.customers[b];
            if (A.demand != B.demand) return A.demand > B.demand;
            return A.latestLeaveTime < B.latestLeaveTime;
        });

    // First-fit append: try to put each customer into an existing route (append),
    // otherwise open a new route. This prioritizes minimizing #routes.
    for (int c : customers)
    {
        bool placed = false;

        for (Route& r : s.routes)
        {
            r.customers.pushBack(c);
            if (evaluateRouteCost(ins, dist_mat, r) != -1)
            {
                placed = true;
                break;
            }
            r.customers.popBack();
        }

        if (!placed)
        {
            Route r = {
		    .customers = DynamicArray<StdAllocator, u16>(alloc, 8),
		    .changed = true,
		    .lastCost = 0.0
	    };
            r.customers.pushBack(c);
            s.routes.pushBack(std::move(r));
        }
    }

    // remove any accidental empty routes (shouldn't happen, but keep it clean)
    //s.routes.erase(
    //    std::remove_if(s.routes.begin(), s.routes.end(),
    //        [](const Route& r){ return r.customers.empty(); }),
    //    s.routes.end());

    return s;
}

void chooseTwoNonEmptyRoutes(std::mt19937 &rng, Solution &s, int &aIdx, int &bIdx) { 
	// NOTE(Erik): No route can every be empty! That invariant must be respected.

	if (s.routes.size() < 2) return;

	std::uniform_int_distribution<int> rPick(0, (int)s.routes.size() - 1);
	aIdx = rPick(rng);
	bIdx = rPick(rng);
}

// --- SHIFT operator: move a non-empty segment from one route to another ---
static void shiftMove(std::mt19937& rng, Solution& s, TmpAllocator &tmpAlloc)
{
    INSTRUMENT_SCOPE("shiftMove");
    if (s.routes.size() < 2) return;

    {
        INSTRUMENT_SCOPE("route_selection");
        int fromIdx = -1, toIdx;
        chooseTwoNonEmptyRoutes(rng, s, fromIdx, toIdx);
        if (fromIdx == -1) return;
        if (fromIdx == toIdx) return;

        Route& from = s.routes[fromIdx];
        Route& to   = s.routes[toIdx];

        const size_t nFrom = from.customers.size();
        if (nFrom == 0) return;

        from.changed = true;
        to.changed = true;

        std::uniform_int_distribution<size_t> startDist(0, nFrom - 1);
        size_t start = startDist(rng);

        std::uniform_int_distribution<size_t> lenDist(1, nFrom - start);
        size_t len = lenDist(rng);

        {
            INSTRUMENT_SCOPE("segment_extraction");
            DynamicArray<TmpAllocator, u16> segment(tmpAlloc, len);
            for (size_t i = 0; i < len; ++i) {
                segment.pushBack(from.customers[start + i]);
            }
            from.customers.eraseRange(start, start + len);

            std::uniform_int_distribution<size_t> insertDist(0, to.customers.size());
            size_t insertPos = insertDist(rng);
            to.customers.insertRangeAt(insertPos, segment, 0, segment.size());
        }

        {
            INSTRUMENT_SCOPE("cleanup");
            if (from.customers.empty()) {
                s.routes.eraseRange(fromIdx, fromIdx + 1);
            }
        }
    }
}

// --- EXCHANGE operator: swap two non-empty segments between two routes ---
static void exchangeMove(std::mt19937& rng, Solution& s, TmpAllocator &tmpAlloc)
{
    INSTRUMENT_SCOPE("exchangeMove");
    if (s.routes.size() < 2) return;

    {
        INSTRUMENT_SCOPE("route_selection");
        int aIdx = -1, bIdx;
        chooseTwoNonEmptyRoutes(rng, s, aIdx, bIdx);
        if (aIdx == -1) return;
        if (aIdx == bIdx) return;

        Route& A = s.routes[aIdx];
        Route& B = s.routes[bIdx];

        A.changed = true;
        B.changed = true;

        const size_t nA = A.customers.size();
        const size_t nB = B.customers.size();
        if (nA == 0 || nB == 0) return;

        std::uniform_int_distribution<size_t> aStartDist(0, nA - 1);
        size_t aStart = aStartDist(rng);
        std::uniform_int_distribution<size_t> aLenDist(1, nA - aStart);
        size_t aLen = aLenDist(rng);

        std::uniform_int_distribution<size_t> bStartDist(0, nB - 1);
        size_t bStart = bStartDist(rng);
        std::uniform_int_distribution<size_t> bLenDist(1, nB - bStart);
        size_t bLen = bLenDist(rng);

        {
            INSTRUMENT_SCOPE("segment_swap");
            A.customers.insertRangeAt(aStart, B.customers, bStart, bStart + bLen);
            B.customers.insertRangeAt(bStart, A.customers, aStart + bLen, aStart + bLen + aLen);
            A.customers.eraseRange(aStart + bLen, aStart + bLen + aLen);
            B.customers.eraseRange(bStart + aLen, bStart + aLen + bLen);
        }

        {
            INSTRUMENT_SCOPE("cleanup");
            if (A.customers.empty() || B.customers.empty()) {
                if (aIdx > bIdx) {
                    if (A.customers.empty()) s.routes.eraseRange(aIdx, aIdx + 1);
                    if (B.customers.empty()) s.routes.eraseRange(bIdx, bIdx + 1);
                } else {
                    if (B.customers.empty()) s.routes.eraseRange(bIdx, bIdx + 1);
                    if (A.customers.empty()) s.routes.eraseRange(aIdx, aIdx + 1);
                }
            }
        }
    }
}

// --- REORDER (rearrange) operator: reposition a non-empty segment within one route ---
static void reorderMove(std::mt19937& rng, Solution& s, TmpAllocator &tmpAlloc)
{
    INSTRUMENT_SCOPE("reorderMove");
	if (s.routes.empty()) return;

	{
		INSTRUMENT_SCOPE("candidate_selection");
		DynamicArray<TmpAllocator, int> candidates(tmpAlloc, s.routes.size());
		for (int i = 0; i < (int)s.routes.size(); ++i) {
			if ((int)s.routes[i].customers.size() >= 2) {
				candidates.pushBack(i);
			}
		}

		if (candidates.empty()) return;

		std::uniform_int_distribution<int> rPick(0, (int)candidates.size() - 1);
		Route *r = &s.routes[candidates[rPick(rng)]];

		r->changed = true;

		const size_t n = r->customers.size();
		if (n < 2) return;

		std::uniform_int_distribution<size_t> startDist(0, n - 1);
		size_t start = startDist(rng);

		std::uniform_int_distribution<size_t> lenDist(1, n - start);
		size_t len = lenDist(rng);

		{
			INSTRUMENT_SCOPE("segment_reposition");
			DynamicArray<TmpAllocator, u16> segment(tmpAlloc, len);
			for (size_t i = 0; i < len; ++i) {
				segment.pushBack(r->customers[start + i]);
			}
			r->customers.eraseRange(start, start + len);

			std::uniform_int_distribution<size_t> insertDist(0, r->customers.size());
			size_t newPos = insertDist(rng);
			r->customers.insertRangeAt(newPos, segment, 0, segment.size());
		}
	}
}


static void printSolution(
        const ProblemInstance& ins,
		float *dist_mat,
        Solution& s,
        bool verbose)
{
    if(!verbose)
        return;

    spdlog::info("Cost {}", evaluateSolution(ins, dist_mat, s));

    for(size_t k=0;k<s.routes.size();++k)
    {
        std::string route =
            fmt::format("Vehicle {} : 0 ",k);

        for(int c : s.routes[k].customers)
        {
            route +=
              fmt::format("-> {} ",
              ins.customers[c].id);
        }

        route += "->0";

        spdlog::info("{}",route);
    }
}

static float dist(const ProblemInstance& ins,int i,int j){

    const Customer &a=ins.customers[i];
    const Customer &b=ins.customers[j];

    double dx=a.x-b.x;
    double dy=a.y-b.y;

    return (float)std::sqrt(dx*dx+dy*dy);
}

static void init_dist_mat(const ProblemInstance &ins, float *dist_mat, size_t num_customers) {
    for (size_t i = 0; i < num_customers; i++) {
		for (size_t j = 0; j < num_customers; j++) {
			dist_mat[i * num_customers + j] = dist(ins, i, j);
		}
    }
}

// TODO: Change print stmts to spdlogging statements for cleaner outputs.
// TODO: add verbose parameter so benchmarks can run without console output
double stochasticLocalSearch(const ProblemInstance& instance, const double timeLimitSeconds, bool verbose){
    INSTRUMENT_SCOPE("stochasticLocalSearch");
	std::mt19937 rng(0); // random seed
	
	using A1 = Freelist<Contiguous<4096 * 4>, 0, 256, Allocator::NoStorage>;
	using A2 = Malloc;
	A1 a1;
	A2 a2;
	Fallback<A1, A2> fallback = Fallback(a1, a2);
	auto eFence = ElectricFence<decltype(fallback)>(fallback);
	StdAllocator alloc = Instrument<decltype(eFence)>(eFence);

	// A1 a1_;
	// A2 a2_;
	// Fallback<A1, A2> fallback_ = Fallback(a1_, a2_);
	TmpAllocator tmpAlloc; // = // ElectricFence<decltype(fallback)>(fallback_);

	size_t num_customers = instance.customers.size();
	float *dist_mat = new float[num_customers * num_customers];
	init_dist_mat(instance, dist_mat, num_customers);

	Solution best = greedyMinVehiclesInit(alloc, instance, dist_mat);
	double bestScore = evaluateSolution(instance, dist_mat, best);

	if(verbose)
	{
		spdlog::info("---- SLS ----");
		spdlog::info(
			"Instance {} | Initial Score {}",
			instance.name,
			bestScore);
	}
	u64 mutations = 0;
	u64 mutStrength = 1;
	double initialScore = bestScore;
	
	auto startTime = std::chrono::high_resolution_clock::now();
	u64 iterationCount = 0;


	TabuList tabuList;
	tabuList.push(best);

	int totalTabuHits =0;
	int tabuHits = 0;
	while (true) {
		INSTRUMENT_SCOPE("SLS_iteration");
		auto currentTime = std::chrono::high_resolution_clock::now();
		std::chrono::duration<double> elapsed = currentTime - startTime;
		if (elapsed.count() >= timeLimitSeconds) {
			break;
		}
		
		iterationCount++;
		Solution neighbor = best;

		// Choose the correct mutation operator
		switch(std::uniform_int_distribution op(0,1); op(rng)) {
			case 0:
				exchangeMove(rng, neighbor, tmpAlloc);
				break;
			case 1:
				shiftMove(rng, neighbor, tmpAlloc);
				break;
			default: exchangeMove(rng, neighbor, tmpAlloc);
		}

		// check all shifts / exchanges
		Solution tmpBest = exchangeMove_Exhaustive(neighbor, tmpAlloc);
		Solution shiftMoveBest = shiftMove_Exhagitustive(neighbor, tmpAlloc);
		if (evaluateSolution( ... ) {
			tmpBest = shiftMoveBest;
		}

		// check all permutations
		Solution reorderMoveBest = reorder_Exhaustive(tmpBest, tmpAlloc);
		if (evaluateSolution( ... ) {
			best = shiftMoveBest;
		}

		neighbor = reorderMoveBest;	

		// Evaluate the neighbor
		double score = evaluateSolution(instance, dist_mat, neighbor);
		if (score == -1) {
			// infeasible candidate => continue
			continue;
		}
		if (tabuList.contains(neighbor)) {
			totalTabuHits++;
			tabuHits++;
			continue; // We skip if we already saw this solution
		}
		// accept if equal or better to walk plateaus in solution space
		if (score <= bestScore){
			best=std::move(neighbor);
			bestScore = score;
			mutStrength = std::min(1000lu, mutStrength * 2);
		} else {
			mutStrength = std::max(1lu, mutStrength / 2);
		}
		tabuList.push(best);


		/* 

		tmpAlloc.deallocateAll(); // reset temporary allocator manually.
		if (tabuList.contains(neighbor)) {
			totalTabuHits++;
			tabuHits++;
			continue; // We skip if we already saw this solution
		}

		// Evaluate the neighbor
		double score = evaluateSolution(instance, dist_mat, neighbor);
		if (score == -1) {
			// infeasible candidate => continue
			continue;
		}
		// accept if equal or better to walk plateaus in solution space
		if (score <= bestScore){
			best=std::move(neighbor);
			bestScore = score;
			mutStrength = std::min(1000lu, mutStrength * 2);
		} else {
			mutStrength = std::max(1lu, mutStrength / 2);
		}
		tabuList.push(best);

		*/
	}

	if(verbose)
	{
		spdlog::info(
		"Best Score {} | Improvement {}",bestScore,
		initialScore - bestScore);

		spdlog::info(
			"TotalTabuHits {}",totalTabuHits);

		spdlog::info (
		"Time {:.3f}s | Iterations {} | Mutations {}", timeLimitSeconds, iterationCount, mutations);

		spdlog::info(
		"Vehicles {} / {}",
		best.routes.size(),
		instance.numberOfVehicles);
	}

	printSolution(instance, dist_mat, best, verbose);

	delete[] dist_mat;

	return bestScore;
}

