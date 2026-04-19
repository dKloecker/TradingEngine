add_library(orderbook INTERFACE
        order.h
        matching_engine.h
        order_book.h
        order_book_listener.h
)
target_include_directories(orderbook INTERFACE ${CMAKE_SOURCE_DIR})
target_link_libraries(orderbook INTERFACE core networking)

if (CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME OR TRADING_BUILD_TESTING)
    add_subdirectory(tests)
    add_subdirectory(benchmarks)
endif ()
