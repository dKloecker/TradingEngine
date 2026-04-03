find_package(Boost REQUIRED)
find_package(benchmark REQUIRED)

add_executable(queue_benchmarks spsc_queue_bench.cpp)

target_link_libraries(queue_benchmarks
        PRIVATE
        core_queue
        benchmark::benchmark_main
        Boost::headers
)

target_compile_options(queue_benchmarks PRIVATE -O3 -march=native)
