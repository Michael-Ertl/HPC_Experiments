cmake_minimum_required(VERSION 3.28)

# Set project info
set(CMAKE_GENERATOR Ninja)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_FLAGS_DEBUG "-g3 -O0 -fno-omit-frame-pointer -mavx2 -mf16c")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -ffast-math -mavx2 -mf16c")
project(hpc VERSION 1.0
        DESCRIPTION "A hpc experiment regarding the Vehicle Routing Problem with Time Windows."
        LANGUAGES C CXX)

# Gather local dependencies
find_package(CUDAToolkit REQUIRED)

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
add_library(solver_lib
        # Add the src cpp files here.
        src/problem_instance.cpp
)
target_include_directories(solver_lib PUBLIC src/)
target_link_libraries(solver_lib PUBLIC spdlog::spdlog)
target_compile_definitions(solver_lib PUBLIC SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)


# Add the core executable to the project
add_executable(core src/entrypoint.cpp)
target_link_libraries(core PRIVATE solver_lib)

# Add the tests and benchmarks to the project
enable_testing()
add_subdirectory(tests)
add_subdirectory(benchmarks)
