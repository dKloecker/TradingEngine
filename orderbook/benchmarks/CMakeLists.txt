find_package(benchmark REQUIRED)

add_executable(orderbook_benchmarks
        orderbook_bench.cpp
)

target_link_libraries(orderbook_benchmarks PRIVATE
        orderbook
        benchmark::benchmark_main
)
target_compile_options(orderbook_benchmarks PRIVATE -O3 -march=native)
