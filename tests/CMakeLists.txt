add_executable(unit_tests
        # Add your test files here.
        test_parser.cpp
        test_cuda_availability.cpp
        test_allocators.cpp
)

target_link_libraries(unit_tests PRIVATE GTest::gtest_main GTest::gmock solver_lib)
target_compile_definitions(unit_tests PRIVATE ENABLE_DEBUG=1 ENABLE_PROFILER=1)
include(GoogleTest)
gtest_discover_tests(unit_tests)
