find_package(benchmark REQUIRED)

add_executable(memory_benchmarks fixed_size_pool_resource_bench.cpp)

target_link_libraries(memory_benchmarks
        PRIVATE
        core_memory
        benchmark::benchmark_main
)

target_compile_options(memory_benchmarks PRIVATE -O3 -march=native)
