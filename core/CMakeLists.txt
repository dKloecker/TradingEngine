add_library(core STATIC
        fixed_size_pool_resource.cpp
        fixed_size_pool_resource.h
        spsc_queue.cpp
        spsc_queue.h
        util.cpp
        util.h
)

target_include_directories(core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if (CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME OR TRADING_BUILD_TESTING)
    add_subdirectory(tests)
    add_subdirectory(benchmarks)
endif ()