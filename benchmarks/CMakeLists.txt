add_executable(benchmarks
        # Add your benchmark files here.
        bench_basic.cpp
        bench_ortools.cpp
)

target_link_libraries(benchmarks PRIVATE benchmark::benchmark_main solver_lib ortools::ortools)
target_compile_definitions(benchmarks PRIVATE
        BENCHMARK_INSTANCES_DIR="${CMAKE_SOURCE_DIR}/benchmark_instances"
)
