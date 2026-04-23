find_package(GTest REQUIRED)

add_executable(memory_tests fixed_size_pool_resource_test.cpp
        pool_resource_test.cpp)
target_link_libraries(memory_tests PRIVATE core_memory GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(memory_tests)
