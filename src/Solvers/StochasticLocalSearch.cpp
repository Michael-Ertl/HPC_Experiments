//
// SIMPLE STOCHASTIC LOCAL SEARCH VRPTW
//

#include "./StochasticLocalSearch.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <random>
#include <vector>
#include <spdlog/spdlog.h>
#include <iostream>

struct Route {
    std::vector<int> customers;
    bool changed;
    double lastCost;
};
struct Solution {
    std::vector<Route> routes;
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

    for(int customerIdx:route.customers){

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
        Solution s){

    int64_t total=0;

    for(Route &route:s.routes) {
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
static Solution greedyMinVehiclesInit(const ProblemInstance& ins, int32_t *dist_mat)
{
    Solution s;

    // customers are 1..n (0 is depot)
    std::vector<int> customers;
    customers.reserve(ins.customers.size() > 0 ? ins.customers.size() - 1 : 0);
    for (int i = 1; i < (int)ins.customers.size(); ++i)
        customers.push_back(i);

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
            r.customers.push_back(c);
            if (evaluateRouteCost(ins, dist_mat, r) != -1)
            {
                placed = true;
                break;
            }
            r.customers.pop_back();
        }

        if (!placed)
        {
            Route r = {
		    .customers = {},
		    .changed = true,
		    .lastCost = 0.0
	    };
            r.customers.push_back(c);
            s.routes.push_back(std::move(r));
        }
    }

    // remove any accidental empty routes (shouldn't happen, but keep it clean)
    s.routes.erase(
        std::remove_if(s.routes.begin(), s.routes.end(),
            [](const Route& r){ return r.customers.empty(); }),
        s.routes.end());

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
    if (fromIdx == -1) return;
    if (fromIdx == -1) return;
    if (fromIdx == -1) return;
    if (fromIdx == toIdx) return;

    Route& from = s.routes[fromIdx];
    Route& to   = s.routes[toIdx];

    const int nFrom = (int)from.customers.size();
    if (nFrom <= 0) return;

    from.changed = true;
    to.changed = true;

    std::uniform_int_distribution<int> startDist(0, nFrom - 1);
    int start = startDist(rng);

    std::uniform_int_distribution<int> lenDist(1, nFrom - start);
    int len = lenDist(rng);

    auto segBegin = from.customers.begin() + start;
    auto segEnd   = segBegin + len;

    std::vector<int> segment(segBegin, segEnd);
    from.customers.erase(segBegin, segEnd);

    std::uniform_int_distribution<int> insertDist(0, (int)to.customers.size());
    int insertPos = insertDist(rng);
    to.customers.insert(to.customers.begin() + insertPos, segment.begin(), segment.end());

    // remove empty routes to keep vehicle count minimal
    s.routes.erase(
        std::remove_if(s.routes.begin(), s.routes.end(),
            [](const Route& r){ return r.customers.empty(); }),
        s.routes.end());
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

    const int nA = (int)A.customers.size();
    const int nB = (int)B.customers.size();
    if (nA == 0 || nB == 0) return;

    std::uniform_int_distribution<int> aStartDist(0, nA - 1);
    int aStart = aStartDist(rng);
    std::uniform_int_distribution<int> aLenDist(1, nA - aStart);
    int aLen = aLenDist(rng);

    std::uniform_int_distribution<int> bStartDist(0, nB - 1);
    int bStart = bStartDist(rng);
    std::uniform_int_distribution<int> bLenDist(1, nB - bStart);
    int bLen = bLenDist(rng);

    auto aBeg = A.customers.begin() + aStart;
    auto aEnd = aBeg + aLen;
    auto bBeg = B.customers.begin() + bStart;
    auto bEnd = bBeg + bLen;

    std::vector<int> segA(aBeg, aEnd);
    std::vector<int> segB(bBeg, bEnd);

    A.customers.erase(aBeg, aEnd);
    B.customers.erase(bBeg, bEnd);

    // insert swapped segments at the original start positions
    A.customers.insert(A.customers.begin() + aStart, segB.begin(), segB.end());
    B.customers.insert(B.customers.begin() + bStart, segA.begin(), segA.end());

    s.routes.erase(
        std::remove_if(s.routes.begin(), s.routes.end(),
            [](const Route& r){ return r.customers.empty(); }),
        s.routes.end());
}

// --- REORDER (rearrange) operator: reposition a non-empty segment within one route ---
static void reorderMove(std::mt19937& rng, Solution& s)
{
    if (s.routes.empty()) return;

    // pick a route with at least 2 customers (so something meaningful can happen)
    std::vector<int> candidates;
    candidates.reserve(s.routes.size());
    for (int i = 0; i < (int)s.routes.size(); ++i)
        if ((int)s.routes[i].customers.size() >= 2)
            candidates.push_back(i);

    if (candidates.empty()) return;

    std::uniform_int_distribution<int> rPick(0, (int)candidates.size() - 1);
    Route& r = s.routes[candidates[rPick(rng)]];
    r.changed = true;

    const int n = (int)r.customers.size();
    if (n < 2) return;

    std::uniform_int_distribution<int> startDist(0, n - 1);
    int start = startDist(rng);

    std::uniform_int_distribution<int> lenDist(1, n - start);
    int len = lenDist(rng);

    auto segBeg = r.customers.begin() + start;
    auto segEnd = segBeg + len;

    std::vector<int> segment(segBeg, segEnd);
    r.customers.erase(segBeg, segEnd);

    // choose new insertion position in the shortened route
    std::uniform_int_distribution<int> insertDist(0, (int)r.customers.size());
    int newPos = insertDist(rng);

    r.customers.insert(r.customers.begin() + newPos, segment.begin(), segment.end());
}


// initializes a solution with one vehicle per customer
static Solution stupidOneVehiclePerCustomerInit(
        const ProblemInstance& ins){

    Solution s;

    // skip depot at index 0
    for(int i=1;i<(int)ins.customers.size();++i){

        Route r;
        r.customers.push_back(i);

        s.routes.push_back(std::move(r));
    }

    return s;
}

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
    s.routes.push_back(std::move(newR));

    r.changed = true;
    r.customers.erase(r.customers.begin() + cut, r.customers.end());
}

static void printSolution(
        const ProblemInstance& ins,
	int32_t *dist_mat,
        const Solution& s,
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
   
    size_t num_customers = instance.customers.size();
    int32_t *dist_mat = new int32_t[num_customers * num_customers];
    init_dist_mat(instance, dist_mat, num_customers);

    Solution best = greedyMinVehiclesInit(instance, dist_mat);
    double bestScore =evaluateSolution(instance, dist_mat, best);

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
    for(int it=0;it<iterations;++it){
        Solution neighbor=best;


        for (int i = 0; i < mutStrenght;++i)
        {
            // Choose the correct mutation operator
            switch(std::uniform_int_distribution op(0,4); op(rng)) {
            case 0:
                reorderMove(rng,neighbor);
                break;
            case 1:
                exchangeMove(rng,neighbor);
                break;
            case 2:
                shiftMove(rng,neighbor);
                break;
            default: exchangeMove(rng,neighbor);;
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
        } else
        {
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
