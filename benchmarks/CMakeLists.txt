add_executable(benchmarks
        # Add your benchmark files here.
        # bench_basic.cpp
        bench_StochasticLocalSearch.cpp

)

find_package(PkgConfig REQUIRED)
pkg_check_modules(PFM libpfm)

target_link_libraries(benchmarks PRIVATE benchmark::benchmark
        benchmark::benchmark_main
        solver_lib ${PFM_LIBRARIES})
