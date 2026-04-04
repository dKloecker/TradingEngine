find_package(GTest REQUIRED)

add_executable(logging_tests
        async_logger_test.cpp
        log_writer_test.cpp
)

target_link_libraries(logging_tests PRIVATE core_logging GTest::gtest_main)
target_compile_definitions(logging_tests PRIVATE TESTING)

include(GoogleTest)
gtest_discover_tests(logging_tests)
