#pragma once

#include "../problem_instance.h"
#include <string>
#include <vector>
#include <cstdint>

struct LocalSearchDescentContext {
    ProblemInstance instance;
    float* distMat;
    std::vector<std::vector<int>> routes;
    double currentCost;
    double initialCost;
    int maxLambda;
    bool verbose;
    std::string timestamp;
    uint64_t iterationCount;
    uint64_t improvementCount;
};

LocalSearchDescentContext localSearchDescentInitialize(const ProblemInstance& instance, bool verbose = false);

void localSearchDescent(LocalSearchDescentContext& ctx);

LocalSearchDescentContext localSearchDescentRun(const ProblemInstance& instance, bool verbose = false);
