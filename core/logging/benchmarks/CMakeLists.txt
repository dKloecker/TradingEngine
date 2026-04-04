find_package(benchmark REQUIRED)

add_executable(logging_benchmarks
        async_logger_bench.cpp
)

target_link_libraries(logging_benchmarks PRIVATE
        core_logging
        benchmark::benchmark_main
)
target_compile_options(logging_benchmarks PRIVATE -O3 -march=native)
target_compile_definitions(logging_benchmarks PRIVATE TESTING)
