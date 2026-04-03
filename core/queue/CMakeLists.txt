add_library(core_queue INTERFACE)
target_include_directories(core_queue INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(core_queue INTERFACE core_util)

if (CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME OR TRADING_BUILD_TESTING)
    add_subdirectory(tests)
    add_subdirectory(benchmarks)
endif()
