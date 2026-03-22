//
// Created by rat on 3/15/26.
//

#ifndef HPC_PARALLELLSD_H
#define HPC_PARALLELLSD_H
#include "problem_instance.h"

struct MetaData {
    ProblemInstance instance;
    std::vector<std::vector<int>> solution;
    int num_threads;
    double currentCost;
    double initialCost;
    int maxLambda;
    bool verbose;
    float* dist;
};


float runParallelLSD(ProblemInstance instance, int num_threads, bool verbose);

#endif //HPC_PARALLELLSD_H