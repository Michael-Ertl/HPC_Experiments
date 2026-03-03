cmake_minimum_required(VERSION 3.28)

# Set project info
set(CMAKE_GENERATOR Ninja)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_FLAGS_DEBUG "-g3 -O0 -fno-omit-frame-pointer -mavx2 -mf16c")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -ffast-math -march=native -mtune=native")
project(hpc VERSION 1.0
        DESCRIPTION "A hpc experiment regarding the Vehicle Routing Problem with Time Windows."
        LANGUAGES C CXX)

# Gather local dependencies
find_package(CUDAToolkit QUIET)
#find_package(CUDAToolkit REQUIRED)

# OR-Tools (prebuilt binary)
set(ORTOOLS_ROOT "${CMAKE_SOURCE_DIR}/submodules/or-tools_x86_64_Ubuntu-24.04_cpp_v9.15.6755")
list(APPEND CMAKE_PREFIX_PATH "${ORTOOLS_ROOT}")
find_package(ortools CONFIG REQUIRED)

# Configure the test and benchmark libraries
add_subdirectory(submodules/googletest)
add_subdirectory(submodules/benchmark)

# Configure the logging library
set(SPDLOG_SUBMODULE_DIR "${CMAKE_SOURCE_DIR}/submodules/spdlog")
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_BENCH OFF CACHE BOOL "" FORCE)
set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)
set(SPDLOG_FMT_EXTERNAL OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_PIC ON CACHE BOOL "" FORCE)
add_subdirectory("${SPDLOG_SUBMODULE_DIR}" EXCLUDE_FROM_ALL) # EXCLUDE_FROM_ALL means it doesn't compile unless the lib is linked into the program.


# Compile the src code into a static library
option(ENABLE_DEBUG "Enable debug checks (e.g. electric fence)" OFF)
option(ENABLE_PROFILER "Enable allocator instrumentation" OFF)

add_library(solver_lib
        # Add the src cpp files here.
        src/problem_instance.cpp
        src/allocators.cpp
        src/Solvers/StochasticLocalSearch.cpp
)
target_include_directories(solver_lib PUBLIC src/)
#target_include_directories(solver_lib PUBLIC ${CUDAToolkit_INCLUDE_DIRS})
target_link_libraries(solver_lib PUBLIC spdlog::spdlog)
#target_link_libraries(solver_lib PUBLIC CUDA::cuda_driver CUDA::cudart)
target_compile_definitions(solver_lib PUBLIC SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)


if (CUDAToolkit_FOUND)
	message(STATUS "CUDA found — enabling CUDA support")

	target_include_directories(solver_lib PUBLIC ${CUDAToolkit_INCLUDE_DIRS})
	target_link_libraries(solver_lib PUBLIC CUDA::cuda_driver CUDA::cudart)
	target_compile_definitions(solver_lib PUBLIC HAVE_CUDA=1)
else()
	message(STATUS "CUDA not found — building without CUDA support")
	target_compile_definitions(solver_lib PUBLIC HAVE_CUDA=0)
endif()



if (ENABLE_DEBUG)
	target_compile_definitions(solver_lib PUBLIC ENABLE_DEBUG=1)
endif()
if (ENABLE_PROFILER)
	target_compile_definitions(solver_lib PUBLIC ENABLE_PROFILER=1)
endif()

# Add the core executable to the project
add_executable(core src/entrypoint.cpp)
target_link_libraries(core PRIVATE solver_lib)

# Add the tests and benchmarks to the project
enable_testing()
add_subdirectory(tests)
add_subdirectory(benchmarks)
