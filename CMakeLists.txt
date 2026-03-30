cmake_minimum_required(VERSION 4.1)
project(trading)

set(CMAKE_CXX_STANDARD 26)

enable_testing()

add_subdirectory(core)
add_library(trading STATIC library.cpp)

target_link_libraries(trading PUBLIC core)