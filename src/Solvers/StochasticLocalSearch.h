//
// Created by michael on 2/19/26.
//
#ifndef STOCHASTICLOCALSEARCH_H
#define STOCHASTICLOCALSEARCH_H

#include "../problem_instance.h"
#include <string>
#include <vector>

struct CustomerInfo {
    int id;
    int x;
    int y;
};

struct OptimizationStats {
    std::string instanceName;
    std::string timestamp;
    double initialScore;
    double finalScore;
    double improvement;
    double relativeImprovement;
    double timeLimitSeconds;
    double elapsedSeconds;
    uint64_t iterationCount;
    uint64_t mutationCount;
    uint64_t totalTabuHits;
    size_t initialVehicles;
    size_t finalVehicles;
    size_t numCustomers;
    uint32_t capacityPerVehicle;
    bool feasible;
    
    // Solution representation for storage
    std::vector<std::vector<int>> routes;  // Each route is a list of customer IDs
    
    // Customer coordinates for visualization (id, x, y)
    std::vector<CustomerInfo> customers;
};

OptimizationStats stochasticLocalSearch(const ProblemInstance& instance, const double timeLimitSeconds, bool verbose=false);
#endif //STOCHASTICLOCALSEARCH_H
