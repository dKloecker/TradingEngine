include(FetchContent)

FetchContent_Declare(
        googlebenchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG v1.8.3
)

set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googlebenchmark)

add_executable(core_benchmarks
        fixed_size_pool_resource_bench.cpp
)

target_link_libraries(core_benchmarks
        PRIVATE
        core
        benchmark::benchmark_main
)

target_compile_options(core_benchmarks PRIVATE -O3 -march=native)