#include "./LocalSearchDescent.h"
#include "../utils.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <limits>
#include <sstream>
#include <chrono>
#include <iostream>
#include <spdlog/spdlog.h>

// =============================================================================
// DATA STRUCTURES
// =============================================================================

/**
 * RouteData - Holds all information about a single vehicle route
 * 
 * @customers: List of customer IDs in visiting order (excluding depot which is always at start/end)
 * @cost: Total cost (time) of traversing this route including travel time + service time
 * @load: Current total demand/cargo on this vehicle
 * @lastArrivalTime: Time when vehicle arrives at last customer in route
 */
struct RouteData {
    std::vector<int> customers;
    double cost;
    int load;
    double lastArrivalTime;
};

/**
 * Move - Represents a relocation move (moving a segment of customers from one route to another)
 * 
 * This is the core move operator for the local search. It represents moving a contiguous
 * segment of customers (lambda customers) from one route to another route at a specific
 * insertion position.
 * 
 * @fromRoute: Index of the route we move customers FROM
 * @toRoute: Index of the route we move customers TO
 * @fromStart: Starting position (index) of the segment in the fromRoute
 * @toStart: Insertion position in the toRoute
 * @segmentLength: Number of customers in the segment (lambda)
 * @deltaCost: Change in total cost if this move is applied (negative = improvement)
 */
struct Move {
    int fromRoute;
    int toRoute;
    int fromStart;
    int toStart;
    int segmentLength;
    double deltaCost;
};

// =============================================================================
// HELPER FUNCTIONS - Distance and Route Calculations
// =============================================================================

/**
 * euclideanDistance - Calculate straight-line distance between two points
 * Uses standard Euclidean distance formula: sqrt((x2-x1)^2 + (y2-y1)^2)
 * 
 * @x1, y1: Coordinates of first point
 * @x2, y2: Coordinates of second point
 * @return: Euclidean distance between the two points
 */
static double euclideanDistance(int x1, int y1, int x2, int y2) {
    double dx = static_cast<double>(x1 - x2);
    double dy = static_cast<double>(y1 - y2);
    return std::sqrt(dx * dx + dy * dy);
}

/**
 * calculateRouteDistance - Calculate total travel distance for a route
 * 
 * Simply sums up distances between consecutive stops (depot -> customer1 -> customer2 -> ... -> depot)
 * Does NOT account for time windows or service times - just raw travel distance.
 * 
 * @customers: Vector of all customers (needed for customer data lookup)
 * @route: Vector of customer IDs in visiting order
 * @distMat: Pre-computed distance matrix for fast lookups
 * @depotIdx: Index of depot in the distance matrix (usually 0)
 * @return: Sum of all travel distances
 */
static double calculateRouteDistance(
    const std::vector<Customer>& customers,
    const std::vector<int>& route,
    const float* distMat,
    int depotIdx) {
    INSTRUMENT_SCOPE("calculateRouteDistance");
    
    if (route.empty()) {
        return 0.0;
    }

    double totalDistance = 0.0;
    int prevIdx = depotIdx;

    for (int customerIdx : route) {
        totalDistance += distMat[prevIdx * customers.size() + customerIdx];
        prevIdx = customerIdx;
    }

    totalDistance += distMat[prevIdx * customers.size() + depotIdx];
    return totalDistance;
}

/**
 * evaluateRouteFeasibility - Check if a route satisfies all constraints AND calculate its cost
 * 
 * This is a critical function that does TWO things:
 * 1. Checks capacity constraint: total demand must not exceed vehicle capacity
 * 2. Checks time window constraints: must arrive before customer's latestLeaveTime
 * 3. Calculates the actual cost considering wait times (if arriving before earliestArrivalTime)
 * 
 * The cost calculation:
 * - Travel from previous stop to current customer
 * - If arrival is before earliestArrivalTime, WAIT until that time (cost = wait time)
 * - If arrival is after latestLeaveTime, route is INFEASIBLE
 * - Add service time at customer
 * 
 * @customers: All customers in the problem
 * @route: The route to evaluate (vector of customer IDs in order)
 * @distMat: Pre-computed distance matrix
 * @depotIdx: Index of depot (usually 0)
 * @capacity: Vehicle capacity constraint
 * @routeCost: OUTPUT - the calculated cost of this route (only valid if returns true)
 * @return: true if route is feasible (satisfies all constraints), false otherwise
 */
static bool evaluateRouteFeasibility(
    const std::vector<Customer>& customers,
    const std::vector<int>& route,
    const float* distMat,
    int depotIdx,
    uint32_t capacity,
    double& routeCost) {
    INSTRUMENT_SCOPE("evaluateRouteFeasibility");
    
    // Empty route is always feasible with zero cost
    if (route.empty()) {
        routeCost = 0.0;
        return true;
    }

    uint32_t load = 0;          // Current vehicle load
    double time = 0.0;          // Current time in route
    double totalCost = 0.0;     // Accumulated cost
    int prevIdx = depotIdx;     // Start from depot

    // Process each customer in the route
    for (int customerIdx : route) {
        const Customer& customer = customers[customerIdx];

        // Check capacity constraint - if exceeded, route is infeasible
        load += customer.demand;
        if (load > capacity) {
            return false;
        }

        // Travel to customer
        double travelDist = distMat[prevIdx * customers.size() + customerIdx];
        time += travelDist;

        // Check time window: must arrive BEFORE latestLeaveTime
        if (time > customer.latestLeaveTime) {
            return false;
        }

        // If arriving too early, wait until earliestArrivalTime
        // This adds waiting time to the cost
        if (customer.earliestArrivalTime > time) {
            time = static_cast<double>(customer.earliestArrivalTime);
        }
        
        // Add service time at customer
        time += customer.serviceTime;
        prevIdx = customerIdx;
    }

    // Return to depot adds final travel cost
    time += distMat[prevIdx * customers.size() + depotIdx];
    routeCost = time;
    return true;
}

// =============================================================================
// MOVE EVALUATION - Core of Local Search
// =============================================================================

/**
 * evaluateMove - Simulate what happens if we move a segment of customers from one route to another
 * 
 * This function does the heavy lifting for local search. It:
 * 1. Constructs the hypothetical new routes after the move
 * 2. Checks if both new routes are feasible (capacity + time windows)
 * 3. Calculates the change in total cost (deltaCost)
 * 
 * The move itself: remove a contiguous segment from fromRoute and insert it
 * at a specific position in toRoute.
 * 
 * Example: If fromRoute = [1,2,3,4,5] and we move segment [2,3] (lambda=2, fromStart=1)
 *          to toRoute = [A,B,C] at position 2 (toStart=2)
 *          Result: fromRoute = [1,4,5], toRoute = [A,B,2,3,C]
 * 
 * @customers: All customers in problem
 * @routes: Current route configurations
 * @distMat: Distance matrix
 * @depotIdx: Depot index
 * @capacity: Vehicle capacity
 * @fromRouteIdx: Index of route to remove segment from
 * @toRouteIdx: Index of route to insert segment into
 * @fromStart: Position in fromRoute where segment starts
 * @segmentLength: Number of customers in segment (lambda)
 * @toInsertPos: Position in toRoute where segment should be inserted
 * @currentFromCost: Current cost of fromRoute (passed in to avoid recalculation)
 * @currentToCost: Current cost of toRoute (passed in to avoid recalculation)
 * @newFromCost: OUTPUT - calculated cost of new fromRoute
 * @newToCost: OUTPUT - calculated cost of new toRoute
 * @deltaCost: OUTPUT - change in total cost (newFromCost + newToCost - oldCosts)
 * @return: true if move is feasible and improves (or doesn't worsen) cost
 */
static bool evaluateMove(
    const std::vector<Customer>& customers,
    const std::vector<RouteData>& routes,
    const float* distMat,
    int depotIdx,
    uint32_t capacity,
    int fromRouteIdx,
    int toRouteIdx,
    int fromStart,
    int segmentLength,
    int toInsertPos,
    double& newFromCost,
    double& newToCost,
    double& deltaCost) {
    INSTRUMENT_SCOPE("evaluateMove");

    const RouteData& fromRoute = routes[fromRouteIdx];
    const RouteData& toRoute = routes[toRouteIdx];

    double currentFromCost = calculateRouteDistance(
        customers, fromRoute.customers, distMat, depotIdx);
    double currentToCost = calculateRouteDistance(
        customers, toRoute.customers, distMat, depotIdx);

    // Bounds checking - ensure segment is valid
    if (fromStart + segmentLength > static_cast<int>(fromRoute.customers.size())) {
        return false;
    }
    if (toInsertPos > static_cast<int>(toRoute.customers.size())) {
        return false;
    }

    // Build new fromRoute by removing the segment
    // We keep all customers EXCEPT those in [fromStart, fromStart+segmentLength)
    std::vector<int> newFromRoute;
    for (size_t i = 0; i < fromRoute.customers.size(); ++i) {
        if (i < static_cast<size_t>(fromStart) || 
            i >= static_cast<size_t>(fromStart + segmentLength)) {
            newFromRoute.push_back(fromRoute.customers[i]);
        }
    }

    // Build new toRoute by inserting the segment at toInsertPos
    std::vector<int> newToRoute;
    newToRoute.reserve(toRoute.customers.size() + segmentLength);
    for (size_t i = 0; i < toRoute.customers.size(); ++i) {
        if (i == static_cast<size_t>(toInsertPos)) {
            // Insert the segment before this position
            for (int j = 0; j < segmentLength; ++j) {
                newToRoute.push_back(fromRoute.customers[fromStart + j]);
            }
        }
        newToRoute.push_back(toRoute.customers[i]);
    }
    // Handle insertion at the end
    if (toInsertPos >= static_cast<int>(toRoute.customers.size())) {
        for (int j = 0; j < segmentLength; ++j) {
            newToRoute.push_back(fromRoute.customers[fromStart + j]);
        }
    }

    // Check if new fromRoute is feasible (time windows + capacity)
    bool fromFeasible = evaluateRouteFeasibility(
        customers, newFromRoute, distMat, depotIdx, capacity, newFromCost);
    
    if (!fromFeasible) {
        return false;
    }

    // Check if new toRoute is feasible (time windows + capacity)
    bool toFeasible = evaluateRouteFeasibility(
        customers, newToRoute, distMat, depotIdx, capacity, newToCost);
    
    if (!toFeasible) {
        return false;
    }

    // Calculate distance cost for new routes
    newFromCost = calculateRouteDistance(
        customers, newFromRoute, distMat, depotIdx);
    newToCost = calculateRouteDistance(
        customers, newToRoute, distMat, depotIdx);

    // Calculate total change in cost
    // Negative deltaCost means improvement!
    deltaCost = (newFromCost + newToCost) - (currentFromCost + currentToCost);
    return true;
}

/**
 * findBestLambdaMove - Find the best relocation move using lambda-exchange neighborhood
 * 
 * Lambda-exchange is a local search technique where we consider moving segments of
 * exactly 'lambda' consecutive customers. Lambda can range from 1 to maxLambda.
 * 
 * For each lambda value, we try ALL possible moves:
 * - Every pair of different routes (fromRoute, toRoute)
 * - Every possible starting position in fromRoute
 * - Every possible insertion position in toRoute
 * 
 * We use first-improving strategy: as soon as we find ANY improving move, we return it.
 * This is faster than best-improving (finding the absolute best move) but may converge
 * to a different local optimum.
 * 
 * @customers: All customers in problem
 * @routes: Current route configurations
 * @distMat: Distance matrix
 * @depotIdx: Depot index
 * @capacity: Vehicle capacity
 * @lambda: Size of segment to move (1 = single customer, 2 = pair, etc.)
 * @return: The best (or first improving) move found, with deltaCost = INF if no move found
 */
static Move findBestLambdaMove(
    const std::vector<Customer>& customers,
    std::vector<RouteData>& routes,
    const float* distMat,
    int depotIdx,
    uint32_t capacity,
    int lambda) {
    INSTRUMENT_SCOPE("findBestLambdaMove");
    
    // Initialize with "no move found" sentinel values
    Move bestMove;
    bestMove.deltaCost = std::numeric_limits<double>::max();
    bestMove.fromRoute = -1;
    bestMove.toRoute = -1;

    // Iterate over all pairs of different routes
    for (size_t fromIdx = 0; fromIdx < routes.size(); ++fromIdx) {
        for (size_t toIdx = 0; toIdx < routes.size(); ++toIdx) {
            // Can't move from a route to itself (would be a different operator)
            if (fromIdx == toIdx) continue;

            const RouteData& fromRoute = routes[fromIdx];
            const RouteData& toRoute = routes[toIdx];

            // Can't move segment larger than route size
            if (fromRoute.customers.size() < static_cast<size_t>(lambda)) continue;

            // Try every possible starting position in fromRoute
            for (int fromStart = 0; fromStart <= static_cast<int>(fromRoute.customers.size()) - lambda; ++fromStart) {
                // Try every possible insertion position in toRoute
                // Position 0 = insert at front, size = insert at back
                for (int toPos = 0; toPos <= static_cast<int>(toRoute.customers.size()); ++toPos) {
                    double newFromCost, newToCost, deltaCost;

                    bool valid = evaluateMove(
                        customers, routes, distMat, depotIdx, capacity,
                        static_cast<int>(fromIdx), static_cast<int>(toIdx),
                        fromStart, lambda, toPos,
                        newFromCost, newToCost, deltaCost);

                    // If move is valid AND improves cost, return immediately (first improvement)
                    if (valid && deltaCost < bestMove.deltaCost) {
                        bestMove.fromRoute = static_cast<int>(fromIdx);
                        bestMove.toRoute = static_cast<int>(toIdx);
                        bestMove.fromStart = fromStart;
                        bestMove.toStart = toPos;
                        bestMove.segmentLength = lambda;
                        bestMove.deltaCost = deltaCost;
                    }
                }
            }
        }
    }

    return bestMove;
}

/**
 * applyMove - Actually perform the relocation move on the route data
 * 
 * This modifies the route structures in-place. It:
 * 1. Extracts the segment from fromRoute
 * 2. Removes it from fromRoute
 * 3. Inserts it into toRoute at the specified position
 * 
 * Note: After applying a move, costs in RouteData will be stale until
 * recalculateAllCosts() is called!
 * 
 * @routes: Vector of routes to modify
 * @move: The move to apply
 */
static void applyMove(
    std::vector<RouteData>& routes,
    const Move& move) {
    INSTRUMENT_SCOPE("applyMove");
    
    RouteData& fromRoute = routes[move.fromRoute];
    RouteData& toRoute = routes[move.toRoute];

    // Extract the segment to move
    std::vector<int> segment;
    for (int i = 0; i < move.segmentLength; ++i) {
        segment.push_back(fromRoute.customers[move.fromStart + i]);
    }

    // Remove segment from fromRoute
    // Note: Each erase shifts elements, so we erase from same position repeatedly
    for (int i = 0; i < move.segmentLength; ++i) {
        fromRoute.customers.erase(fromRoute.customers.begin() + move.fromStart);
    }

    // Insert segment into toRoute
    toRoute.customers.insert(
        toRoute.customers.begin() + move.toStart,
        segment.begin(),
        segment.end());
}

/**
 * calculateTotalCost - Sum up costs of all routes
 * 
 * Simple helper to get the total cost of the entire solution
 * 
 * @routes: All routes in current solution
 * @return: Sum of all individual route costs
 */
static double calculateTotalCost(const std::vector<RouteData>& routes) {
    INSTRUMENT_SCOPE("calculateTotalCost");
    double total = 0.0;
    for (const auto& route : routes) {
        total += route.cost;
    }
    return total;
}

/**
 * removeEmptyRoutes - Clean up any routes that have become empty
 * 
 * After applying moves, some routes may become empty (no customers).
 * This removes them from the vector to keep things clean.
 * 
 * @routes: Vector of routes to clean
 */
static void removeEmptyRoutes(std::vector<RouteData>& routes) {
    INSTRUMENT_SCOPE("removeEmptyRoutes");
    routes.erase(
        std::remove_if(routes.begin(), routes.end(),
            [](const RouteData& r) { return r.customers.empty(); }),
        routes.end());
}

/**
 * recalculateAllCosts - Update cost field for all routes
 * 
 * After making changes to routes (applying moves), the cost fields
 * become stale. This recalculates them by re-evaluating each route.
 * 
 * @customers: All customers in problem
 * @routes: Routes to update (modified in place)
 * @distMat: Distance matrix
 * @depotIdx: Depot index
 * @capacity: Vehicle capacity
 */
static void recalculateAllCosts(
    const std::vector<Customer>& customers,
    std::vector<RouteData>& routes,
    const float* distMat,
    int depotIdx,
    uint32_t capacity) {
    INSTRUMENT_SCOPE("recalculateAllCosts");
    
    for (auto& route : routes) {
        route.cost = calculateRouteDistance(
            customers, route.customers, distMat, depotIdx);
    }
}

// =============================================================================
// MAIN LOCAL SEARCH DESCENT ALGORITHM
// =============================================================================

/**
 * localSearchDescentInitialize - Build initial solution and set up the algorithm
 * 
 * This is the "constructor" phase. It:
 * 1. Creates the distance matrix (computed once for efficiency)
 * 2. Builds an initial feasible solution using a greedy heuristic
 * 3. Returns a context object with all necessary data
 * 
 * Initial Solution Strategy (Greedy + Savings-like):
 * - Sort customers by demand (descending) and time windows (ascending)
 * - Try to add each customer to the first route that can accommodate it
 * - If no existing route works, create a new route
 * 
 * This is a VERY naive initial solution - the local search will improve it significantly!
 * 
 * @instance: The VRP problem instance to solve
 * @verbose: Whether to print progress messages
 * @return: Initialized context with starting solution
 */
LocalSearchDescentContext localSearchDescentInitialize(const ProblemInstance& instance, bool verbose) {
    INSTRUMENT_SCOPE("localSearchDescentInitialize");
    LocalSearchDescentContext ctx;
    
    // Copy problem instance and set basic parameters
    ctx.instance = instance;
    ctx.verbose = verbose;
    ctx.maxLambda = 5;              // We'll try moving 1-5 customer segments
    ctx.iterationCount = 0;         // Total moves attempted
    ctx.improvementCount = 0;       // Moves that actually improved

    // Step 1: Build distance matrix
    // Pre-compute all pairwise distances - O(n^2) but saves repeated calculations
    size_t numCustomers = instance.customers.size();
    ctx.distMat = new float[numCustomers * numCustomers];

    for (size_t i = 0; i < numCustomers; ++i) {
        for (size_t j = 0; j < numCustomers; ++j) {
            const Customer& c1 = instance.customers[i];
            const Customer& c2 = instance.customers[j];
            ctx.distMat[i * numCustomers + j] = static_cast<float>(
                euclideanDistance(c1.x, c1.y, c2.x, c2.y));
        }
    }

    // Step 2: Get list of all customers (except depot at index 0)
    std::vector<int> unrouted;
    for (size_t i = 1; i < instance.customers.size(); ++i) {
        unrouted.push_back(static_cast<int>(i));
    }

    // Step 3: Sort customers - heuristic for initial solution
    // Priority: high demand first, then tight time windows
    // This tends to create good initial routes
    std::sort(unrouted.begin(), unrouted.end(),
        [&](int a, int b) {
            const Customer& ca = instance.customers[a];
            const Customer& cb = instance.customers[b];
            if (ca.demand != cb.demand) return ca.demand > cb.demand;  // High demand first
            return ca.latestLeaveTime < cb.latestLeaveTime;           // Tight windows first
        });

    // Step 4: Build initial routes greedily
    // For each customer, try to add to first route that can fit it
    std::vector<RouteData> routes;
    for (int customer : unrouted) {
        bool placed = false;
        
        // Try each existing route
        for (auto& route : routes) {
            route.customers.push_back(customer);
            double tempCost;
            if (evaluateRouteFeasibility(
                    instance.customers, route.customers, ctx.distMat, 0,
                    instance.capacityPerVehicle, tempCost)) {
                placed = true;
                break;
            }
            // Doesn't fit - remove and try next route
            route.customers.pop_back();
        }
        
        // Can't fit in any existing route? Create a new one
        if (!placed) {
            RouteData newRoute;
            newRoute.customers.push_back(customer);
            routes.push_back(newRoute);
        }
    }

    // Step 5: Calculate initial costs for all routes
    for (auto& route : routes) {
        route.cost = calculateRouteDistance(
            instance.customers, route.customers, ctx.distMat, 0);
    }

    // Step 6: Store initial solution in context
    ctx.currentCost = calculateTotalCost(routes);
    ctx.initialCost = ctx.currentCost;

    ctx.routes.clear();
    for (const auto& route : routes) {
        ctx.routes.push_back(route.customers);
    }

    // Timestamp for logging/identification
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t_now), "%Y-%m-%d %H:%M:%S");
    ctx.timestamp = ss.str();

    return ctx;
}

/**
 * localSearchDescent - Main local search optimization loop
 * 
 * This implements the classic local search descent (hill climbing) algorithm:
 * 
 * 1. Start with initial solution (from context)
 * 2. Repeat until no improvement found:
 *    a. For lambda = 1 to maxLambda:
 *       - Find the best move of this lambda size
 *       - If it improves cost, apply it and restart the loop
 *    b. If no lambda found an improvement, we're done!
 * 
 * This is "first-improvement" with lambda-exchange neighborhood.
 * It stops at the first local optimum.
 * 
 * Key characteristics:
 * - Uses lambda-exchange (segments of 1-5 customers)
 * - First-improvement (stops at first improving move)
 * - Relocation only (no reversals or swaps)
 * - Stops at local optimum
 * 
 * @ctx: Context containing initial solution and parameters (modified in place)
 */
void localSearchDescent(LocalSearchDescentContext& ctx) {
    INSTRUMENT_SCOPE("localSearchDescent");
    // Extract frequently used data for performance
    const ProblemInstance& instance = ctx.instance;
    float* distMat = ctx.distMat;
    int depotIdx = 0;
    uint32_t capacity = instance.capacityPerVehicle;
    const std::vector<Customer>& customers = instance.customers;
    int maxLambda = ctx.maxLambda;

    // Convert route vectors to RouteData structures (includes cost tracking)
    std::vector<RouteData> routes;
    for (const auto& routeVec : ctx.routes) {
        RouteData rd;
        rd.customers = routeVec;
        rd.cost = calculateRouteDistance(customers, rd.customers, distMat, depotIdx);
        routes.push_back(rd);
    }

    // Main local search loop
    // Keep improving until we can't find any improving move
    bool improved = true;
    while (improved) {
        improved = false;

        // =========================================================================
        // EVALUATION SECTION: Reevaluate all routes and total cost
        // =========================================================================
        recalculateAllCosts(customers, routes, distMat, depotIdx, capacity);
        ctx.currentCost = calculateTotalCost(routes);
        // =========================================================================

        // Try each lambda value in sequence (1, 2, 3, 4, 5)
        for (int lambda = 1; lambda <= maxLambda; ++lambda) {
            // Find best move for this lambda
            Move bestMove = findBestLambdaMove(
                customers, routes, distMat, depotIdx, capacity, lambda);

            // If we found an improving move (deltaCost < -0.001 for numerical stability)
            if (bestMove.fromRoute >= 0 && bestMove.deltaCost < -0.001) {
                // Apply the move
                applyMove(routes, bestMove);

                // Reevaluate all routes and total cost after the move
                recalculateAllCosts(customers, routes, distMat, depotIdx, capacity);
                ctx.currentCost = calculateTotalCost(routes);

                // Update statistics
                ctx.iterationCount++;
                ctx.improvementCount++;
                improved = true;

                // Optional logging
                if (ctx.verbose) {
                    spdlog::info("Lambda {}: Improved by {:.2f}, Total Cost: {:.2f}",
                        lambda, -bestMove.deltaCost, ctx.currentCost);
                }

                // Start over with lambda=1 (first improvement strategy)
                break;
            }
        }
    }

    // Final cleanup: remove any empty routes and recalculate costs one last time
    removeEmptyRoutes(routes);
    recalculateAllCosts(customers, routes, distMat, depotIdx, capacity);

    ctx.currentCost = calculateTotalCost(routes);
    ctx.routes.clear();
    for (const auto& route : routes) {
        ctx.routes.push_back(route.customers);
    }
}

/**
 * localSearchDescentRun - Top-level function to run the entire algorithm
 * 
 * Convenience wrapper that:
 * 1. Initializes the context (builds initial solution)
 * 2. Runs verbose output if requested
 * 3. Executes the local search
 * 4. Prints final statistics
 * 
 * @instance: The VRP problem to solve
 * @verbose: Whether to print detailed progress
 * @return: Final context with optimized solution
 */
LocalSearchDescentContext localSearchDescentRun(const ProblemInstance& instance, bool verbose) {
    INSTRUMENT_SCOPE("localSearchDescentRun");
    // Initialize - build starting solution
    LocalSearchDescentContext ctx = localSearchDescentInitialize(instance, verbose);
    
    if (verbose) {
        spdlog::info("---- Local Search Descent ----");
        spdlog::info("Instance: {}", instance.name);
        spdlog::info("Initial Cost: {:.2f}", ctx.initialCost);
    }

    // Run the optimization
    localSearchDescent(ctx);

    if (verbose) {
        spdlog::info("Final Cost: {:.2f}", ctx.currentCost);
        spdlog::info("Improvement: {:.2f}", ctx.initialCost - ctx.currentCost);
        spdlog::info("Iterations: {}", ctx.iterationCount);
    }

    return ctx;
}
