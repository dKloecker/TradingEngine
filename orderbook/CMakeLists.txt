add_library(orderbook INTERFACE)
target_include_directories(orderbook INTERFACE ${CMAKE_SOURCE_DIR})
target_link_libraries(orderbook INTERFACE core)

if (CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME OR TRADING_BUILD_TESTING)
    add_subdirectory(tests)
endif ()
