find_package(PkgConfig REQUIRED)
find_package(PkgConfig QUIET)

if(PkgConfig_FOUND)
    pkg_check_modules(PFM QUIET libpfm libpfm4)
endif()

# Fallback if pkg-config failed
if(NOT PFM_FOUND)
    find_path(PFM_INCLUDE_DIR perfmon/pfmlib.h)
    find_library(PFM_LIBRARY NAMES pfm)

    if(PFM_INCLUDE_DIR AND PFM_LIBRARY)
        set(PFM_FOUND TRUE)
        set(PFM_LIBRARIES ${PFM_LIBRARY})
        set(PFM_INCLUDE_DIRS ${PFM_INCLUDE_DIR})
    endif()
endif()

if(NOT PFM_FOUND)
    message(FATAL_ERROR "libpfm not found")
endif()


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
target_include_directories(benchmarks PRIVATE ${PFM_INCLUDE_DIRS})