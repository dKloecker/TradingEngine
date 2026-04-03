find_package(GTest REQUIRED)

add_executable(queue_tests spsc_queue_test.cpp)
target_link_libraries(queue_tests PRIVATE core_queue GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(queue_tests)
