#include "problem_instance.h"

#include <fstream>
#include <filesystem>
#include <stdexcept>

ProblemInstance readInstance(const std::string &filePath) {
    std::ifstream file(filePath);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file: " + filePath);
    }

    ProblemInstance instance;
    std::string line;

    // Line 1: Instance name
    std::getline(file, instance.name);
    if (!instance.name.empty() && instance.name.back() == '\r') {
        instance.name.pop_back();
    }

    // Skip until we find "VEHICLE"
    while (std::getline(file, line)) {
        if (line.find("VEHICLE") != std::string::npos) break;
    }

    // Skip "NUMBER CAPACITY" header line
    std::getline(file, line);

    // Read vehicle count and capacity
    file >> instance.numberOfVehicles >> instance.capacityPerVehicle;

    // Skip until we find "CUSTOMER"
    while (std::getline(file, line)) {
        if (line.find("CUSTOMER") != std::string::npos) break;
    }

    // Skip column header line
    std::getline(file, line);

    // Read customer data
    Customer c;
    while (file >> c.id >> c.x >> c.y >> c.demand
                >> c.earliestArrivalTime >> c.latestLeaveTime >> c.serviceTime) {
        instance.customers.push_back(c);
    }

    return instance;
}

std::vector<ProblemInstance> readAllInstances(const std::string &directory) {
    std::vector<ProblemInstance> instances;

    for (const auto &entry : std::filesystem::directory_iterator(directory)) {
        if (entry.is_regular_file() && entry.path().extension() == ".txt") {
            instances.push_back(readInstance(entry.path().string()));
        }
    }

    return instances;
}
