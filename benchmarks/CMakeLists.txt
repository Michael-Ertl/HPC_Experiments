find_package(PkgConfig REQUIRED)
pkg_check_modules(PFM libpfm)

add_executable(benchmarks
        # Add your benchmark files here.
        # bench_basic.cpp
        bench_StochasticLocalSearch.cpp
        bench_ortools.cpp
)

target_link_libraries(benchmarks PRIVATE benchmark::benchmark benchmark::benchmark_main
        solver_lib ortools::ortools ${PFM_LIBRARIES})
target_compile_definitions(benchmarks PRIVATE
        BENCHMARK_INSTANCES_DIR="${CMAKE_SOURCE_DIR}/benchmark_instances"
)
