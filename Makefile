.PHONY: prepare-debug, prepare-release, build

prepare-debug:
	cmake -B build -DCMAKE_BUILD_TYPE=Debug

prepare-release:
	cmake -B build -DCMAKE_BUILD_TYPE=Release

build:
	cmake --build build -j 32 # num threads

run-tests:
	./build/tests/unit_tests
