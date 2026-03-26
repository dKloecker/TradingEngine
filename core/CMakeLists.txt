add_library(core STATIC
        pool_allocator.cpp
        pool_allocator.h
)

target_include_directories(core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME OR TRADING_BUILD_TESTING)
    add_subdirectory(tests)
    add_subdirectory(benchmarks)
endif()