//
// Created by michael on 2/17/26.
//
#include "VRPTW_Instance.h"

#include <stdio.h>
#include <string.h>  // strstr
#include <stdlib.h>  // not needed yet, but will be later

int read_instance(VRPTW *instance, const char *file) {

    // DEFAULT Values
    strcpy(instance->name, "default");
    instance->number_of_vehicles = 0;
    instance->capacity_per_vehicle = 0;
    instance->number_of_customers = 0;
    instance->customers = NULL;


    // Open the file
    FILE *filePath = fopen(file, "r");
    if (filePath == NULL) {
        return 1;
    }

    // Read Name
    fgets(instance->name, sizeof(instance->name), filePath);

    //Read rest of meta data
    char dummy[100];
    fscanf(filePath, "%s", dummy); // Reads "VEHICLE"
    fscanf(filePath, "%s", dummy); // Reads "NUMBER"
    fscanf(filePath, "%s", dummy); // Reads "CAPACITY"

    // 3. Now grab the actual values using the arrow operator
    fscanf(filePath, "%d", &instance->number_of_vehicles);
    fscanf(filePath, "%d", &instance->capacity_per_vehicle);


    fclose(filePath);
    return 0;
}
void free_instance(VRPTW *instance) {
    return;
}