add_executable(benchmarks
        # Add your benchmark files here.
        bench_simple.cpp
)

target_link_libraries(benchmarks PRIVATE benchmark::benchmark_main solver_lib)
