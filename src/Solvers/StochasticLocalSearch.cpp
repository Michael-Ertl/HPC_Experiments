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

struct Route {
    std::vector<int> customers;
};
struct Solution {
    std::vector<Route> routes;
};

static int32_t dist(const ProblemInstance& ins,int i,int j){

    const Customer &a=ins.customers[i];
    const Customer &b=ins.customers[j];

    double dx=a.x-b.x;
    double dy=a.y-b.y;

    return (int32_t)std::lround(std::sqrt(dx*dx+dy*dy));
}

// returns -1 on infeasible instance
// returns total route distance/cost otherwise
static double evaluateRouteCost(
        const ProblemInstance& instance,
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

        double travelDistance = dist(instance,previousVisitIdx,customerIdx);

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

    distanceSum+=dist(instance,previousVisitIdx,depotIdx); // return to depot

    return distanceSum;
}

// returns -1 on infeasible route
// returns total fleetCost otherwise
static double evaluateSolution(
        const ProblemInstance& ins,
        Solution s){

    int64_t total=0;

    for(Route &route:s.routes){

        double routeCost = evaluateRouteCost(ins,route);

        if(routeCost == -1){ // route infeasible
            return -1;
        }

        total+=routeCost;
    }

    return total;
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
    if(rng()%2)
        A.customers.insert(
            A.customers.end(),
            B.customers.begin(),
            B.customers.end());
    else
        A.customers.insert(
            A.customers.begin(),
            B.customers.begin(),
            B.customers.end());

    s.routes.erase(
        s.routes.begin()+b);
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

    int customer =
        from.customers[idx];

    from.customers.erase(
        from.customers.begin()+idx);

    Route& to =
        s.routes[rDist(rng)];

    std::uniform_int_distribution<int>
        insert(
            0,
            to.customers.size());

    to.customers.insert(
        to.customers.begin()+insert(rng),
        customer);
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

    Route newR;
    newR.customers.assign(r.customers.begin() + cut, r.customers.end());
    r.customers.erase(r.customers.begin() + cut, r.customers.end());

    s.routes.push_back(std::move(newR));
}


static void printSolution(const ProblemInstance& ins, const Solution& s){

    std::cout<<"Cost "<< evaluateSolution(ins, s) <<"\n";

    for(size_t k=0;k<s.routes.size();++k){

        std::cout<<"Vehicle "<<k<<" : 0 ";

        for(int c:s.routes[k].customers)
            std::cout<<"-> "<<ins.customers[c].id<<" ";

        std::cout<<"->0\n";
    }
}

int stochasticLocalSearch(const ProblemInstance& instance, const int iterations){
    std::mt19937 rng(0); // random seed

    Solution best = stupidOneVehiclePerCustomerInit(instance);
    double bestScore =evaluateSolution(instance,best);
    std::cout<<"\n--- SLS ---\n"<<"Initial solution:\n" <<"Score: " << bestScore << "\n";
    double initialScore = bestScore;
    for(int it=0;it<iterations;++it){
        Solution neighbor=best;

        // Choose the correct mutation operator
        switch(std::uniform_int_distribution op(0,4); op(rng)) {
            case 0:
                mergeRoutesMove(rng,neighbor);
                break;
            case 1:
                splitRouteMove(rng,neighbor);
                break;
            case 2:
                relocateMove(rng,neighbor);
                break;
            default: relocateMove(rng,neighbor);;
        }

        // Evaluate the neighbor
        double score = evaluateSolution(instance, neighbor);
        if (score == -1) {
            // infeasible candidate => continue
            continue;
        }
        // accept if equal or better to walk plateaus in solution space
        if (score <= bestScore){
            best=std::move(neighbor);
            bestScore = score;
        }
    }

    std::cout<<"\nBEST\n" << "Improvement: " << initialScore - bestScore<<"\nUsed Vehicles: "<< best.routes.size() << "/" << instance.numberOfVehicles <<"\n";
    printSolution(instance,best);
    return 0;
}