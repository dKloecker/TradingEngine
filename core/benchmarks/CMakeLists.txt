# Boost
find_package(Boost REQUIRED)

# Google Benchmark
find_package(benchmark REQUIRED)

add_executable(core_benchmarks
        fixed_size_pool_resource_bench.cpp
        spsc_queue_bench.cpp
)

target_link_libraries(core_benchmarks
        PRIVATE
        core
        benchmark::benchmark_main
        Boost::headers
)

target_compile_options(core_benchmarks PRIVATE -O3 -march=native)