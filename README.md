# HPC_Experiments
- In this project we experiment with efficient implementations for the Vehicle Routing Problem with Time Windows (VRPTW).


## Rules
- C++ code is only allowed for convenience in low performance areas of the code. 
- As soon as high performance is required, don't use any c++ features such as the standard library.

# Running the program
You can either make an unoptimized build with `make prepare-debug` or an optimized build with `make prepare-release`.
Then you can do the following options:

## Core program
Run with `make build run-program`.

## Testing
We use [Google Test](https://github.com/google/googletest).
Run the tests with `make build run-tests`.

## Benchmarking
We use [Google Benchmark](https://github.com/google/benchmark).
Run the benchmarks with `make build run-benchmarks`.

## Logging
We use [spdlog](https://github.com/gabime/spdlog).
Logging is only enabled in a debug build.