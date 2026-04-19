find_package(GTest REQUIRED)

add_executable(order_book_tests order_book_tests.cpp)
target_link_libraries(order_book_tests PRIVATE orderbook GTest::gtest_main)

add_executable(matching_engine_tests matching_engine_tests.cpp)
target_link_libraries(matching_engine_tests PRIVATE orderbook GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(order_book_tests)
gtest_discover_tests(matching_engine_tests)
