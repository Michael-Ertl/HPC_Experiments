#pragma once

#include "util.h"

struct Customer {
    int32_t id;
    int32_t x;
    int32_t y;
    uint32_t demand;
    uint32_t earliestArrivalTime;
    uint32_t latestLeaveTime;
    uint32_t serviceTime;
};

struct ProblemInstance {
    std::string name;
    uint32_t numberOfVehicles;
    uint32_t capacityPerVehicle;
    std::vector<Customer> customers;
};

ProblemInstance readInstance(const std::string &directory);

std::vector<ProblemInstance> readAllInstances(const std::string &directory);
