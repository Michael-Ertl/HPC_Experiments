//
// Created by michael on 2/17/26.
//

#ifndef VRPTW_INSTANCE_H
#define VRPTW_INSTANCE_H
typedef struct {
    int id;
    double x;
    double y;
    int demand;
    int earliest_arrival_time;
    int latest_leave_time;
    int service_time;
} Customer;

typedef struct {
    char name[9];
    int number_of_vehicles;
    int capacity_per_vehicle;
    int number_of_customers;

    Customer *customers;
} VRPTW;

int read_instance(VRPTW *instance, const char *file); // return 0 if it ran correctly
void free_instance(VRPTW *instance);
#endif //VRPTW_INSTANCE_H
