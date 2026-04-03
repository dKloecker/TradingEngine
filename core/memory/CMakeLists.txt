add_library(core_memory INTERFACE)
target_include_directories(core_memory INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(core_memory INTERFACE core_util)

if (CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME OR TRADING_BUILD_TESTING)
    add_subdirectory(tests)
    add_subdirectory(benchmarks)
endif()
