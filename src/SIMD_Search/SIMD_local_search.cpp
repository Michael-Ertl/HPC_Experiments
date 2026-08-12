#include <chrono>
#include <cstdint>
#include <fstream>
#include <immintrin.h>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <vector>

struct Point {
    int32_t x;
    int32_t y;
};

const int ITERATION_LIMIT = 1e9;
const int SIMD_LANE_COUNT = 8;
const int TRIAL_COUNT = 10;
const int32_t STEP_SIZE = 1;
const int32_t START_RANGE = 10000;
const int32_t TARGET_FITNESS = 90;

struct SearchResult {
    Point best;
    int32_t fitness;
    int iterations;
    bool success;
};

struct SimdSearchResult {
    std::vector<Point> lanes;
    Point best;
    int32_t bestFitness;
    int iterations;
    bool success;
};

int32_t square(int32_t value) { return value * value; }

int32_t fitness(const Point &point) {
    return 100 - square(point.x) - square(point.y);
}

__m256i SIMD_fitness(__m256i x, __m256i y) {
    __m256i x_squared = _mm256_mullo_epi32(x, x);
    __m256i y_squared = _mm256_mullo_epi32(y, y);
    __m256i squared_sum = _mm256_add_epi32(x_squared, y_squared);
    return _mm256_sub_epi32(_mm256_set1_epi32(100), squared_sum);
}

Point mutation(const Point &point, int32_t stepSize, std::mt19937 &rng) {
    std::uniform_int_distribution<int32_t> randomMove(-stepSize, stepSize);

    return Point{
        point.x + randomMove(rng),
        point.y + randomMove(rng),
    };
}

SearchResult run(Point current, std::mt19937 &rng) {
    int32_t stepSize = STEP_SIZE;
    int32_t currentFitness = fitness(current);

    if (currentFitness > TARGET_FITNESS) {
        return SearchResult{current, currentFitness, 0, true};
    }

    for (int i = 0; i < ITERATION_LIMIT; ++i) {
        Point candidate = mutation(current, stepSize, rng);
        int32_t candidateFitness = fitness(candidate);
        if (candidateFitness >= currentFitness) {
            current = candidate;
            currentFitness = candidateFitness;
        }

        if (currentFitness > TARGET_FITNESS) {
            return SearchResult{current, currentFitness, i + 1, true};
        }
    }
    return SearchResult{current, currentFitness, ITERATION_LIMIT, false};
}

SimdSearchResult SIMD_run(const std::vector<Point> &starts, std::mt19937 &rng) {
    int32_t x_coords[SIMD_LANE_COUNT];
    int32_t y_coords[SIMD_LANE_COUNT];

    for (int i = 0; i < SIMD_LANE_COUNT; ++i) {
        x_coords[i] = starts[i].x;
        y_coords[i] = starts[i].y;
    }

    __m256i simd_x =
        _mm256_loadu_si256(reinterpret_cast<const __m256i *>(x_coords));
    __m256i simd_y =
        _mm256_loadu_si256(reinterpret_cast<const __m256i *>(y_coords));
    __m256i fitness_vec = SIMD_fitness(simd_x, simd_y);
    __m256i target_vec = _mm256_set1_epi32(TARGET_FITNESS);
    int iterations = 0;

    for (int i = 0; i < ITERATION_LIMIT; i++) {
        if (_mm256_movemask_epi8(_mm256_cmpgt_epi32(fitness_vec, target_vec)) !=
            0) {
            break;
        }

        // Load an operator
        std::uniform_int_distribution<int32_t> randomMove(-STEP_SIZE,
                                                          STEP_SIZE);
        int32_t move_x = randomMove(rng);
        int32_t move_y = randomMove(rng);
        __m256i move_x_vec = _mm256_set1_epi32(move_x);
        __m256i move_y_vec = _mm256_set1_epi32(move_y);

        // Apply both operators
        __m256i simd_x_new, simd_y_new;
        simd_x_new = _mm256_add_epi32(simd_x, move_x_vec);
        simd_y_new = _mm256_add_epi32(simd_y, move_y_vec);

        __m256i new_fitness_vec = SIMD_fitness(simd_x_new, simd_y_new);
        __m256i improved_mask =
            _mm256_cmpgt_epi32(new_fitness_vec, fitness_vec);

        simd_x = _mm256_blendv_epi8(simd_x, simd_x_new, improved_mask);
        simd_y = _mm256_blendv_epi8(simd_y, simd_y_new, improved_mask);
        fitness_vec =
            _mm256_blendv_epi8(fitness_vec, new_fitness_vec, improved_mask);
        iterations = i + 1;
    }

    int32_t final_x[SIMD_LANE_COUNT];
    int32_t final_y[SIMD_LANE_COUNT];
    int32_t final_fitness[SIMD_LANE_COUNT];
    _mm256_storeu_si256(reinterpret_cast<__m256i *>(final_x), simd_x);
    _mm256_storeu_si256(reinterpret_cast<__m256i *>(final_y), simd_y);
    _mm256_storeu_si256(reinterpret_cast<__m256i *>(final_fitness),
                        fitness_vec);

    std::vector<Point> points;
    for (int i = 0; i < SIMD_LANE_COUNT; ++i) {
        points.push_back(Point{final_x[i], final_y[i]});
    }

    int bestLane = 0;
    for (int lane = 1; lane < SIMD_LANE_COUNT; ++lane) {
        if (final_fitness[lane] > final_fitness[bestLane]) {
            bestLane = lane;
        }
    }

    return SimdSearchResult{points, points[bestLane], final_fitness[bestLane],
                            iterations,
                            final_fitness[bestLane] > TARGET_FITNESS};
}

int main() {
    std::random_device rd;
    std::mt19937 rng(rd());
    std::uniform_int_distribution<int32_t> startDist(-START_RANGE, START_RANGE);

    std::ofstream csv("SIMD_results.csv");
    csv << std::setprecision(17);
    csv << "trial,target_fitness,scalar_seconds,simd_seconds,speedup,"
           "scalar_iterations,simd_iterations,scalar_success,simd_success,"
           "winner,scalar_start_x,scalar_start_y,scalar_best_x,scalar_best_y,"
           "scalar_fitness";
    for (int lane = 0; lane < SIMD_LANE_COUNT; ++lane) {
        csv << ",simd_lane" << lane << "_start_x,simd_lane" << lane
            << "_start_y,simd_lane" << lane << "_best_x,simd_lane" << lane
            << "_best_y,simd_lane" << lane << "_fitness";
    }
    csv << ",simd_best_fitness\n";

    double totalScalarSeconds = 0.0;
    double totalSimdSeconds = 0.0;
    int scalarWins = 0;
    int simdWins = 0;
    int ties = 0;
    int scalarSuccesses = 0;
    int simdSuccesses = 0;

    for (int trial = 0; trial < TRIAL_COUNT; ++trial) {
        std::vector<Point> starts;
        for (int lane = 0; lane < SIMD_LANE_COUNT; ++lane) {
            starts.push_back(Point{startDist(rng), startDist(rng)});
        }

        SearchResult scalarResult{starts[0], fitness(starts[0]), 0, false};
        Point scalarWinningStart = starts[0];
        int scalarIterations = 0;
        auto scalarStartTime = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < SIMD_LANE_COUNT; ++i) {
            SearchResult result = run(starts[i], rng);
            scalarIterations += result.iterations;
            if (result.fitness > scalarResult.fitness) {
                scalarResult = result;
                scalarWinningStart = starts[i];
            }
            if (result.success) {
                scalarResult = result;
                scalarWinningStart = starts[i];
                break;
            }
        }
        auto scalarEndTime = std::chrono::high_resolution_clock::now();
        double scalarSeconds =
            std::chrono::duration<double>(scalarEndTime - scalarStartTime)
                .count();

        auto simdStartTime = std::chrono::high_resolution_clock::now();
        SimdSearchResult simdResult = SIMD_run(starts, rng);
        auto simdEndTime = std::chrono::high_resolution_clock::now();
        double simdSeconds =
            std::chrono::duration<double>(simdEndTime - simdStartTime).count();

        totalScalarSeconds += scalarSeconds;
        totalSimdSeconds += simdSeconds;
        scalarSuccesses += scalarResult.success ? 1 : 0;
        simdSuccesses += simdResult.success ? 1 : 0;

        const char *winner = "tie";
        if (simdResult.success && !scalarResult.success) {
            winner = "simd";
            ++simdWins;
        } else if (scalarResult.success && !simdResult.success) {
            winner = "scalar";
            ++scalarWins;
        } else if (scalarResult.success && simdResult.success) {
            if (simdSeconds < scalarSeconds) {
                winner = "simd";
                ++simdWins;
            } else if (scalarSeconds < simdSeconds) {
                winner = "scalar";
                ++scalarWins;
            } else {
                ++ties;
            }
        } else if (simdResult.bestFitness > scalarResult.fitness) {
            winner = "simd";
            ++simdWins;
        } else if (scalarResult.fitness > simdResult.bestFitness) {
            winner = "scalar";
            ++scalarWins;
        } else {
            ++ties;
        }

        double speedup = simdSeconds > 0.0 ? scalarSeconds / simdSeconds : 0.0;
        csv << trial << ',' << TARGET_FITNESS << ',' << scalarSeconds << ','
            << simdSeconds << ',' << speedup << ',' << scalarIterations << ','
            << simdResult.iterations << ','
            << (scalarResult.success ? "true" : "false") << ','
            << (simdResult.success ? "true" : "false") << ',' << winner << ','
            << scalarWinningStart.x << ',' << scalarWinningStart.y << ','
            << scalarResult.best.x << ',' << scalarResult.best.y << ','
            << scalarResult.fitness;
        for (int lane = 0; lane < SIMD_LANE_COUNT; ++lane) {
            csv << ',' << starts[lane].x << ',' << starts[lane].y << ','
                << simdResult.lanes[lane].x << ',' << simdResult.lanes[lane].y
                << ',' << fitness(simdResult.lanes[lane]);
        }
        csv << ',' << simdResult.bestFitness << '\n';
    }

    csv << "summary,trials,target_fitness,total_scalar_seconds,"
           "total_simd_seconds,average_speedup,scalar_wins,simd_wins,ties,"
           "scalar_successes,simd_successes\n";
    csv << "summary," << TRIAL_COUNT << ',' << TARGET_FITNESS << ','
        << totalScalarSeconds << ',' << totalSimdSeconds << ','
        << (totalSimdSeconds > 0.0 ? totalScalarSeconds / totalSimdSeconds
                                   : 0.0)
        << ',' << scalarWins << ',' << simdWins << ',' << ties << ','
        << scalarSuccesses << ',' << simdSuccesses << '\n';

    std::cout << std::setprecision(17);
    std::cout << "Target fitness: " << TARGET_FITNESS << '\n';
    std::cout << "Total scalar seconds: " << totalScalarSeconds << '\n';
    std::cout << "Total SIMD seconds: " << totalSimdSeconds << '\n';
    std::cout << "Average speedup: "
              << (totalSimdSeconds > 0.0 ? totalScalarSeconds / totalSimdSeconds
                                         : 0.0)
              << '\n';
    std::cout << "Scalar wins: " << scalarWins << '\n';
    std::cout << "SIMD wins: " << simdWins << '\n';
    std::cout << "Ties: " << ties << '\n';
    std::cout << "Scalar successes: " << scalarSuccesses << '/' << TRIAL_COUNT
              << '\n';
    std::cout << "SIMD successes: " << simdSuccesses << '/' << TRIAL_COUNT
              << '\n';

    return 0;
}
