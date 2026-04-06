add_library(core_logging STATIC
        async_logger.cpp
        async_logger.h
        logger_enums.h
        logger_utils.h
)

target_include_directories(core_logging PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(core_logging PUBLIC core_queue)

if (CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME OR TRADING_BUILD_TESTING)
    target_compile_definitions(core_logging PRIVATE TESTING)
    add_subdirectory(tests)
    add_subdirectory(benchmarks)
endif ()