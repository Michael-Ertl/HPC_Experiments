#include <gtest/gtest.h>
#include "problem_instance.h"

TEST(ParserTest, FindsAllFiles) {
    EXPECT_EQ(readAllInstances("benchmark_instances").size(), 56);
}

TEST(ParserTest, ParsesCorrectly) {
    ProblemInstance problem = readInstance("benchmark_instances/r201.txt");
    EXPECT_EQ(problem.name, "R201");
    EXPECT_EQ(problem.numberOfVehicles, 25);
    EXPECT_EQ(problem.capacityPerVehicle, 1000);
    EXPECT_EQ(problem.customers.size(), 101);

    Customer customer = problem.customers[26];
    EXPECT_EQ(customer.id, 26);
    EXPECT_EQ(customer.x, 45);
    EXPECT_EQ(customer.y, 30);
    EXPECT_EQ(customer.demand, 17);
    EXPECT_EQ(customer.earliestArrivalTime, 588);
    EXPECT_EQ(customer.latestLeaveTime, 667);
    EXPECT_EQ(customer.serviceTime, 10);
}