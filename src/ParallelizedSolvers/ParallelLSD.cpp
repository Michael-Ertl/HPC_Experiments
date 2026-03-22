//
// Created by rat on 3/15/26.
//

#include "ParallelLSD.h"

#include <iostream>




float euclideanDist(float x1, float y1, float x2, float y2) {
    float dx = static_cast<double>(x1 - x2);
    float dy = static_cast<double>(y1 - y2);
    return std::sqrt(dx * dx + dy * dy);
}

void calculateDistanceMatrix(MetaData* data) {
    int n = data->instance.customers.size();
    float* matrix = data->dist;
    for (int i = 0; i < data->instance.customers.size(); i++) {
        for (int j = 0; j < data->instance.customers.size(); j++) {
            matrix[n*i + j]+= euclideanDist(data->instance.customers[i].x, data->instance.customers[i].y,
                data->instance.customers[j].x, data->instance.customers[j].y);
        }
    }
}

void evaluateSolution(MetaData* data) {

}


void optimize(MetaData* data) {
    int n = data->instance.customers.size();
    float* matrix = data->dist;

    bool foundMove = true;
    while (foundMove) {
        evaluateSolution(data);
        foundMove = false;
    }
}

float runParallelLSD(ProblemInstance instance, int num_threads, bool verbose) {

    // Intitialize Metadata
    MetaData* data = new MetaData();
    data->verbose = verbose;
    data->num_threads = num_threads;
    data->instance = instance;
    data->currentCost = -1; // TODO: Replace this
    data->initialCost = -1; // TODO: Replace this
    data->maxLambda = 5;
    data->dist = new float[instance.customers.size()*instance.customers.size()];
    calculateDistanceMatrix(data);

    // Run Optimization loop
    optimize(data);

    // Print output
    const float cost = data->currentCost;
    free(data->dist);
    free(data);
    return cost;
}