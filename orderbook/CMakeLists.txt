add_library(orderbook STATIC
        order_book.cpp
        order.cpp
)
target_include_directories(orderbook PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(orderbook PUBLIC core)

if (CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME OR TRADING_BUILD_TESTING)
    add_subdirectory(tests)
endif ()
