.PHONY: prepare-debug prepare-release build

debug-run: prepare-debug build run-program

prepare-debug:
	cmake -B build -DCMAKE_BUILD_TYPE=Debug

prepare-release:
	cmake -B build -DCMAKE_BUILD_TYPE=Release

build:
	cmake --build build -j 32 # num threads

run-program:
	./build/core

run-tests:
	./build/tests/unit_tests

run-benchmarks:
	./build/benchmarks/benchmarks \
	  --benchmark_perf_counters=CYCLES,INSTRUCTIONS,CACHE-MISSES,BRANCH-MISSES \
	  --benchmark_counters_tabular=true

cachegrind:
	valgrind --tool=cachegrind --cache-sim=yes --branch-sim=yes --cachegrind-out-file=analysis/cachegrind.out ./build/core

visualize-cachegrind:
	kcachegrind analysis/cachegrind.out

ANALYSIS_DIR := analysis
FLAMEGRAPH := submodules/FlameGraph
perf-timeline: prepare-release build
	mkdir -p $(ANALYSIS_DIR)
	# record performance data
	perf record -F 4000 --call-graph dwarf -o analysis/perf.data ./build/core
	# convert to script
	perf script -i $(ANALYSIS_DIR)/perf.data > $(ANALYSIS_DIR)/out.perf
	# collapse stacks
	$(FLAMEGRAPH)/stackcollapse-perf.pl $(ANALYSIS_DIR)/out.perf > $(ANALYSIS_DIR)/folded.perf
	# generate flamegraph svg
	$(FLAMEGRAPH)/flamegraph.pl $(ANALYSIS_DIR)/folded.perf > $(ANALYSIS_DIR)/flamegraph.svg


