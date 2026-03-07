#include <random>
#include <string>
#include <sstream>
#include <sqlite3.h>

#include "utils.h"
#include "./Solvers/StochasticLocalSearch.h"
#include "./problem_instance.h"
#include <spdlog/spdlog.h>

static int callback(void *NotUsed, int argc, char **argv, char **azColName) {
   for(int i = 0; i < argc; i++) {
      LOG_INFO("{} = {}", azColName[i], argv[i] ? argv[i] : "NULL");
   }
   return 0;
}

static void initDatabase(sqlite3* db) {
    const char* createTableSQL = 
        "CREATE TABLE IF NOT EXISTS optimization_stats ("
        "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "    instance_name TEXT NOT NULL,"
        "    timestamp TEXT NOT NULL,"
        "    initial_score REAL NOT NULL,"
        "    final_score REAL NOT NULL,"
        "    improvement REAL NOT NULL,"
        "    relative_improvement REAL NOT NULL,"
        "    time_limit_seconds REAL NOT NULL,"
        "    elapsed_seconds REAL NOT NULL,"
        "    iteration_count INTEGER NOT NULL,"
        "    mutation_count INTEGER NOT NULL,"
        "    total_tabu_hits INTEGER NOT NULL,"
        "    initial_vehicles INTEGER NOT NULL,"
        "    final_vehicles INTEGER NOT NULL,"
        "    num_customers INTEGER NOT NULL,"
        "    capacity_per_vehicle INTEGER NOT NULL,"
        "    feasible INTEGER NOT NULL,"
        "    routes TEXT NOT NULL,"
        "    customers TEXT NOT NULL"
        ");";
    
    char* errMsg = nullptr;
    int rc = sqlite3_exec(db, createTableSQL, callback, 0, &errMsg);
    if (rc != SQLITE_OK) {
        LOG_ERROR("SQL error creating table: {}", errMsg);
        sqlite3_free(errMsg);
    } else {
        LOG_INFO("Database table initialized successfully");
    }
}

static std::string routesToString(const std::vector<std::vector<int>>& routes) {
    std::stringstream ss;
    ss << "[";
    for (size_t i = 0; i < routes.size(); ++i) {
        ss << "[";
        for (size_t j = 0; j < routes[i].size(); ++j) {
            ss << routes[i][j];
            if (j < routes[i].size() - 1) ss << ",";
        }
        ss << "]";
        if (i < routes.size() - 1) ss << ",";
    }
    ss << "]";
    return ss.str();
}

static std::string customersToString(const std::vector<CustomerInfo>& customers) {
    std::stringstream ss;
    ss << "[";
    for (size_t i = 0; i < customers.size(); ++i) {
        ss << "{\"id\":" << customers[i].id << ",\"x\":" << customers[i].x << ",\"y\":" << customers[i].y << "}";
        if (i < customers.size() - 1) ss << ",";
    }
    ss << "]";
    return ss.str();
}

static void insertStats(sqlite3* db, const OptimizationStats& stats) {
    const char* insertSQL = 
        "INSERT INTO optimization_stats ("
        "    instance_name, timestamp, initial_score, final_score, improvement, "
        "    relative_improvement, time_limit_seconds, elapsed_seconds, "
        "    iteration_count, mutation_count, total_tabu_hits, "
        "    initial_vehicles, final_vehicles, num_customers, "
        "    capacity_per_vehicle, feasible, routes, customers"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
    
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db, insertSQL, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        LOG_ERROR("Failed to prepare statement: {}", sqlite3_errmsg(db));
        return;
    }
    
    std::string routesStr = routesToString(stats.routes);
    std::string customersStr = customersToString(stats.customers);
    
    sqlite3_bind_text(stmt, 1, stats.instanceName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, stats.timestamp.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 3, stats.initialScore);
    sqlite3_bind_double(stmt, 4, stats.finalScore);
    sqlite3_bind_double(stmt, 5, stats.improvement);
    sqlite3_bind_double(stmt, 6, stats.relativeImprovement);
    sqlite3_bind_double(stmt, 7, stats.timeLimitSeconds);
    sqlite3_bind_double(stmt, 8, stats.elapsedSeconds);
    sqlite3_bind_int64(stmt, 9, static_cast<sqlite3_int64>(stats.iterationCount));
    sqlite3_bind_int64(stmt, 10, static_cast<sqlite3_int64>(stats.mutationCount));
    sqlite3_bind_int64(stmt, 11, static_cast<sqlite3_int64>(stats.totalTabuHits));
    sqlite3_bind_int64(stmt, 12, static_cast<sqlite3_int64>(stats.initialVehicles));
    sqlite3_bind_int64(stmt, 13, static_cast<sqlite3_int64>(stats.finalVehicles));
    sqlite3_bind_int64(stmt, 14, static_cast<sqlite3_int64>(stats.numCustomers));
    sqlite3_bind_int64(stmt, 15, static_cast<sqlite3_int64>(stats.capacityPerVehicle));
    sqlite3_bind_int(stmt, 16, stats.feasible ? 1 : 0);
    sqlite3_bind_text(stmt, 17, routesStr.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 18, customersStr.c_str(), -1, SQLITE_TRANSIENT);
    
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        LOG_ERROR("Failed to insert stats: {}", sqlite3_errmsg(db));
    } else {
        LOG_INFO("Successfully inserted stats for instance: {}", stats.instanceName);
    }
    
    sqlite3_finalize(stmt);
}

int main() {
    spdlog::flush_on(spdlog::level::trace);
    spdlog::set_level(spdlog::level::trace);
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] %^[%l]%$ [%s:%# %!] %v");

    LOG_INFO("Program startup.");
    
    // Open database
    sqlite3* db;
    int rc = sqlite3_open("optimization_results.db", &db);
    if (rc) {
        LOG_ERROR("Can't open database: {}", sqlite3_errmsg(db));
        return 1;
    }
    LOG_INFO("Database opened successfully");
    
    // Initialize database schema
    initDatabase(db);

    std::vector<ProblemInstance> instances =
        readAllInstances("./benchmark_instances");

    size_t i = 0;
    for (const auto& instance : instances) {
        LOG_INFO("Processing instance {}/{}: {}", i + 1, instances.size(), instance.name);
        
        OptimizationStats stats = stochasticLocalSearch(instance, 120.0, true);
        
        // Insert stats into database
        insertStats(db, stats);
        
        LOG_INFO("Completed instance {}: Initial={}, Final={}, Improvement={:.2f}%", 
                 stats.instanceName, stats.initialScore, stats.finalScore, 
                 stats.relativeImprovement * 100.0);
        
		i++;
		break;
    }
    
    // Close database
    sqlite3_close(db);
    LOG_INFO("Program completed. Results saved to optimization_results.db");
    
    return 0;
}
