//
// Created by michael on 2/17/26.
//
#include <stdio.h>

#include <stdio.h>
#include "VRPTW_Instance.h"



// AI stub main method for testing
int main() {
    VRPTW problem;

    int rc = read_instance(&problem, "/home/michael/Documents/HPC_tests/Benchmark_Instances/c101.txt");
    if (rc != 0) {
        printf("readInstance failed with code %d\n", rc);
        return 1;
    }

    printf("Vehicles: %d\n", problem.number_of_vehicles);
    printf("Capacity: %d\n", problem.capacity_per_vehicle);
    printf("Name: %s\n", problem.name);

    free_instance(&problem);
    return 0;
}

