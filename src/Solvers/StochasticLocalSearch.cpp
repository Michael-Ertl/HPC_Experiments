//
// SIMPLE STOCHASTIC LOCAL SEARCH VRPTW
//

#include "./StochasticLocalSearch.h"
#include "../allocators.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <new>
#include <random>
#include <type_traits>
#include <spdlog/spdlog.h>
#include <iostream>

using StdAllocator = Allocator::Malloc;

template<class A, typename T>
class DynamicArray {
	A &a;
	Allocator::TypedBlock<T> block;
	size_t used = 0;

	static size_t nextCapacity(size_t current) {
		if (current == 0) return 8;  // Initial capacity
		return current * 2;  // Exponential growth
	}

	void destroyElements(size_t start, size_t end) {
		for (size_t i = start; i < end; ++i) {
			std::destroy_at(block.ptr + i);
		}
	}

	void grow(size_t minCapacity) {
		size_t newCapacity = capacity();
		while (newCapacity < minCapacity) {
			newCapacity = nextCapacity(newCapacity);
		}
		
		// Try in-place expansion first
		size_t newBytes = newCapacity * sizeof(T);
		size_t deltaBytes = newBytes - block.size;
		if (deltaBytes > 0) {
			Allocator::Block blk = block;
			if (a.expand(blk, deltaBytes)) {
				block = blk;
				return;
			}
		}
		
		// Fall back to reallocation
		if constexpr (std::is_trivially_move_constructible_v<T> && std::is_trivially_destructible_v<T>) {
			Allocator::Block blk = block;
			a.reallocate(blk, newBytes);
			block = blk;
			return;
		}

		Allocator::Block oldBlk = block;
		Allocator::Block newBlk = a.allocate(newBytes);
		T *newPtr = reinterpret_cast<T *>(newBlk.ptr);
		std::uninitialized_move(block.ptr, block.ptr + used, newPtr);
		destroyElements(0, used);
		a.deallocate(oldBlk);
		block = newBlk;
	}

public:
	DynamicArray(A &alloc) : a(alloc) {
		Allocator::Block blk = a.allocate(8 * sizeof(T));  // Initial capacity
		block = blk;
	}

	~DynamicArray() {
		if (block.ptr != nullptr) {
			destroyElements(0, used);
			a.deallocate(block);
		}
	}

	DynamicArray(const DynamicArray &other) : a(other.a) {
		Allocator::Block blk = a.allocate(other.block.size);
		block = blk;
		used = other.used;
		if (used > 0 && block.ptr != nullptr) {
			std::uninitialized_copy(other.block.ptr, other.block.ptr + used, block.ptr);
		}
	}
	DynamicArray &operator=(const DynamicArray &) = delete;

	// Enable move
	DynamicArray(DynamicArray &&other) noexcept : a(other.a) {
		block = other.block;
		used = other.used;
		other.block.ptr = nullptr;
		other.block.size = 0;
		other.used = 0;
	}

	DynamicArray &operator=(DynamicArray &&other) noexcept {
		if (this != &other) {
			// Deallocate current
			if (block.ptr != nullptr) {
				destroyElements(0, used);
				a.deallocate(block);
			}
			// Take from other
			block = other.block;
			used = other.used;
			other.block.ptr = nullptr;
			other.block.size = 0;
			other.used = 0;
		}
		return *this;
	}

	T *begin() const { return block.ptr; }
	T *end() const { return block.ptr + used; }
	const T *cbegin() const { return block.ptr; }
	const T *cend() const { return block.ptr + used; }

	size_t size() const { return used; }
	size_t capacity() const { return block.size / sizeof(T); }
	bool empty() const { return used == 0; }

	T &operator[](size_t idx) { return block.ptr[idx]; }
	const T &operator[](size_t idx) const { return block.ptr[idx]; }

	void reserve(size_t n) {
		if (n > capacity()) {
			grow(n);
		}
	}

	void pushBack(const T &value) {
		if (used >= capacity()) {
			grow(used + 1);
		}
		::new (static_cast<void *>(block.ptr + used)) T(value);
		++used;
	}

	void pushBack(T &&value) {
		if (used >= capacity()) {
			grow(used + 1);
		}
		::new (static_cast<void *>(block.ptr + used)) T(std::move(value));
		++used;
	}

	void popBack() {
		if (used > 0) {
			--used;
			std::destroy_at(block.ptr + used);
		}
	}

	void clear() {
		destroyElements(0, used);
		used = 0;
	}

	void eraseRange(size_t startInclusive, size_t endExclusive) {
		if (startInclusive >= endExclusive || startInclusive >= used) return;
		if (endExclusive > used) endExclusive = used;
		
		size_t count = endExclusive - startInclusive;
		// Move elements left over the erased range
		for (size_t i = endExclusive; i < used; ++i) {
			block.ptr[i - count] = std::move(block.ptr[i]);
		}
		// Destroy trailing moved-from elements
		destroyElements(used - count, used);
		used -= count;
	}

	void insertRangeAt(size_t insertPos, const DynamicArray<A, T> &other, size_t startInclusive, size_t endExclusive) {
		if (startInclusive >= endExclusive) return;
		if (endExclusive > other.used) endExclusive = other.used;
		if (insertPos > used) insertPos = used;
		
		size_t count = endExclusive - startInclusive;
		if (count == 0) return;
		
		// Make room
		size_t oldUsed = used;
		reserve(used + count);
		
		// Shift existing elements to the right
		for (size_t i = oldUsed; i > insertPos; --i) {
			size_t src = i - 1;
			size_t dest = src + count;
			if (dest < oldUsed) {
				block.ptr[dest] = std::move(block.ptr[src]);
			} else {
				::new (static_cast<void *>(block.ptr + dest)) T(std::move(block.ptr[src]));
			}
		}
		
		// Copy from other
		for (size_t i = 0; i < count; ++i) {
			size_t dest = insertPos + i;
			if (dest < oldUsed) {
				block.ptr[dest] = other.block.ptr[startInclusive + i];
			} else {
				::new (static_cast<void *>(block.ptr + dest)) T(other.block.ptr[startInclusive + i]);
			}
		}
		
		used = oldUsed + count;
	}

	void insertRange(const DynamicArray<A, T> &other, size_t startInclusive, size_t endExclusive) {
		insertRangeAt(used, other, startInclusive, endExclusive);
	}
};

struct Route {
    DynamicArray<StdAllocator, u16> customers;
    bool changed;
    double lastCost;
};

struct Solution {
    DynamicArray<StdAllocator, Route> routes;
};


// returns -1 on infeasible instance
// returns total route distance/cost otherwise
static double evaluateRouteCost(
        const ProblemInstance& instance,
	int32_t *dist_mat,
        const Route& route){

    int depotIdx=0;
    int load=0;
    double time=0;
    double distanceSum=0;
    int previousVisitIdx=depotIdx;

    for(int customerIdx : route.customers) {

        const Customer& customer = instance.customers[customerIdx];

        load+=customer.demand;

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

        distanceSum+=travelDistance +  waitingTime;
        previousVisitIdx=customerIdx;
    }

    distanceSum+=dist_mat[previousVisitIdx * instance.customers.size() + depotIdx]; // return to depot

    return distanceSum;
}

// returns -1 on infeasible route
// returns total fleetCost otherwise
static double evaluateSolution(
        const ProblemInstance& ins,
	int32_t *dist_mat,
        const Solution &s){

    int64_t total=0;

    for(Route &route : s.routes) {
	double routeCost = 0;
	if (route.changed) {
		routeCost = evaluateRouteCost(ins, dist_mat, route);
		if(routeCost == -1){ // route infeasible
		    return -1;
		}

		route.changed = false;
		route.lastCost = routeCost;
	} else {
		routeCost = route.lastCost;
	}
        total+=routeCost;
    }
    return total;
}



// --- Greedy init: use as few vehicles as possible (cost quality is irrelevant) ---
static Solution greedyMinVehiclesInit(StdAllocator &alloc, const ProblemInstance& ins, int32_t *dist_mat)
{
    Solution s = Solution{
	    .routes = DynamicArray<StdAllocator, Route>(alloc)
    };

    // customers are 1..n (0 is depot)
    DynamicArray<StdAllocator, u16> customers(alloc);
    customers.reserve(ins.customers.size() > 0 ? ins.customers.size() - 1 : 0);
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
		    .customers = DynamicArray<StdAllocator, u16>(alloc),
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
static void shiftMove(std::mt19937& rng, Solution& s)
{
    if (s.routes.size() < 2) return;

    // collect indices of non-empty routes
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

    StdAllocator segAlloc;
    DynamicArray<StdAllocator, u16> segment(segAlloc);
    segment.reserve(len);
    for (size_t i = 0; i < len; ++i) {
        segment.pushBack(from.customers[start + i]);
    }

    from.customers.eraseRange(start, start + len);

    std::uniform_int_distribution<size_t> insertDist(0, to.customers.size());
    size_t insertPos = insertDist(rng);
    to.customers.insertRangeAt(insertPos, segment, 0, segment.size());

    // remove empty routes to keep vehicle count minimal
    if (from.customers.empty()) {
        s.routes.eraseRange(fromIdx, fromIdx + 1);
    }
}

// --- EXCHANGE operator: swap two non-empty segments between two routes ---
static void exchangeMove(std::mt19937& rng, Solution& s)
{
    if (s.routes.size() < 2) return;

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

    StdAllocator segAllocA;
    DynamicArray<StdAllocator, u16> segA(segAllocA);
    segA.reserve(aLen);
    for (size_t i = 0; i < aLen; ++i) {
        segA.pushBack(A.customers[aStart + i]);
    }

    StdAllocator segAllocB;
    DynamicArray<StdAllocator, u16> segB(segAllocB);
    segB.reserve(bLen);
    for (size_t i = 0; i < bLen; ++i) {
        segB.pushBack(B.customers[bStart + i]);
    }

    A.customers.eraseRange(aStart, aStart + aLen);
    B.customers.eraseRange(bStart, bStart + bLen);

    // insert swapped segments at the original start positions
    A.customers.insertRangeAt(aStart, segB, 0, segB.size());
    B.customers.insertRangeAt(bStart, segA, 0, segA.size());

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

// --- REORDER (rearrange) operator: reposition a non-empty segment within one route ---
static void reorderMove(std::mt19937& rng, Solution& s)
{
	if (s.routes.empty()) return;

	// pick a route with at least 2 customers (so something meaningful can happen)
    StdAllocator candAlloc;
    DynamicArray<StdAllocator, int> candidates(candAlloc);
    candidates.reserve(s.routes.size());
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

    StdAllocator segAlloc;
    DynamicArray<StdAllocator, u16> segment(segAlloc);
    segment.reserve(len);
    for (size_t i = 0; i < len; ++i) {
        segment.pushBack(r->customers[start + i]);
    }

    r->customers.eraseRange(start, start + len);

	// choose new insertion position in the shortened route
    std::uniform_int_distribution<size_t> insertDist(0, r->customers.size());
    size_t newPos = insertDist(rng);

    r->customers.insertRangeAt(newPos, segment, 0, segment.size());
}


#if false
// initializes a solution with one vehicle per customer
static Solution stupidOneVehiclePerCustomerInit(
        const ProblemInstance& ins){

    Solution s;

    // skip depot at index 0
    for(int i=1;i<(int)ins.customers.size();++i){

        Route r;
        r.customers.pushBack(i);

        s.routes.pushBack(std::move(r));
    }

    return s;
}
#endif

#if false
// MUTATIONS OPERATIONS:
static void mergeRoutesMove(
        std::mt19937& rng,
        Solution& s){

    if(s.routes.size()<2)
        return;

    std::uniform_int_distribution<int>
        dist(0,s.routes.size()-1);

    int a=dist(rng);
    int b=dist(rng);

    if(a==b) return;

    Route& A=s.routes[a];
    Route& B=s.routes[b];

    if(B.customers.empty())
        return;

    // random prepend or append
    A.changed = true;
    if(rng()%2) {
        A.customers.insert(
            A.customers.end(),
            B.customers.begin(),
            B.customers.end());
    } else {
        A.customers.insert(
            A.customers.begin(),
            B.customers.begin(),
            B.customers.end());
    }

    s.routes.erase(s.routes.begin()+b);
}
#endif

#if false
static void relocateMove(
        std::mt19937& rng,
        Solution& s){

    if(s.routes.empty())
        return;

    std::uniform_int_distribution<int>
        rDist(0,s.routes.size()-1);

    Route& from =
        s.routes[rDist(rng)];

    if(from.customers.empty())
        return;

    std::uniform_int_distribution<int>
        posFrom(0,from.customers.size()-1);

    int idx=posFrom(rng);

    int customer = from.customers[idx];

    from.changed = true;
    from.customers.erase(from.customers.begin()+idx);

    Route& to = s.routes[rDist(rng)];

    std::uniform_int_distribution<int> insert( 0, to.customers.size());
    to.changed = true;
    to.customers.insert(to.customers.begin() + insert(rng), customer);
}
#endif

#if false
static void splitRouteMove(std::mt19937& rng, Solution& s) {
    if (s.routes.empty()) return;

    std::uniform_int_distribution<int> routeDist(0, (int)s.routes.size() - 1);
    int rIdx = routeDist(rng);

    Route& r = s.routes[rIdx];
    if (r.customers.size() < 2) return; // cannot split

    // cut in [1, size-1] so both sides non-empty
    std::uniform_int_distribution<int> cutDist(1, (int)r.customers.size() - 1);
    int cut = cutDist(rng);

    Route newR = {
	    .customers = {},
	    .changed = true,
	    .lastCost = 0.0
    };
    newR.customers.assign(r.customers.begin() + cut, r.customers.end());
    s.routes.pushBack(std::move(newR));

    r.changed = true;
    r.customers.erase(r.customers.begin() + cut, r.customers.end());
}
#endif

static void printSolution(
        const ProblemInstance& ins,
	int32_t *dist_mat,
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

static int32_t dist(const ProblemInstance& ins,int i,int j){

    const Customer &a=ins.customers[i];
    const Customer &b=ins.customers[j];

    double dx=a.x-b.x;
    double dy=a.y-b.y;

    return (int32_t)std::lround(std::sqrt(dx*dx+dy*dy));
}

static void init_dist_mat(const ProblemInstance &ins, int32_t *dist_mat, size_t num_customers) {
    for (size_t i = 0; i < num_customers; i++) {
	for (size_t j = 0; j < num_customers; j++) {
		dist_mat[i * num_customers + j] = dist(ins, i, j);
	}
    }
}

// TODO: Change print stmts to spdlogging statements for cleaner outputs.
// TODO: add verbose parameter so benchmarks can run without console output
double stochasticLocalSearch(const ProblemInstance& instance, const int iterations, bool verbose){
	std::mt19937 rng(0); // random seed
	StdAllocator alloc;

	size_t num_customers = instance.customers.size();
	int32_t *dist_mat = new int32_t[num_customers * num_customers];
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
	int mutations = 0;
	int mutStrenght = 1;
	double initialScore = bestScore;
	for(int it=0;it<iterations;++it) {
		Solution neighbor = best;

		for (int i = 0; i < mutStrenght;++i)
		{
			// Choose the correct mutation operator
			switch(std::uniform_int_distribution op(0,4); op(rng)) {
				case 0:
					reorderMove(rng, neighbor);
					break;
				case 1:
					exchangeMove(rng,neighbor);
					break;
				case 2:
					shiftMove(rng,neighbor);
					break;
				default: exchangeMove(rng, neighbor);
			}
			mutations++;
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
			mutStrenght = std::min(iterations, mutStrenght * 2);
		} else {
			mutStrenght = std::max(1, mutStrenght / 2);
		}
	}

	if(verbose)
	{
		spdlog::info(
		"Best Score {} | Improvement {}",bestScore,
		initialScore - bestScore);

		spdlog::info (
		"Iterations {} | Mutations {}", iterations, mutations);

		spdlog::info(
		"Vehicles {} / {}",
		best.routes.size(),
		instance.numberOfVehicles);
	}

	printSolution(instance, dist_mat, best, verbose);

	return bestScore;
}
