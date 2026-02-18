add_executable(unit_tests
        # Add your test files here.
        test_parser.cpp
)

target_link_libraries(unit_tests PRIVATE GTest::gtest_main solver_lib)
include(GoogleTest)
gtest_discover_tests(unit_tests)
