.PHONY: prepare-debug, prepare-release, build

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
	./build/benchmarks/benchmarks
