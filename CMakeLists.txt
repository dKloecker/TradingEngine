set(CMAKE_TOOLCHAIN_FILE "$ENV{HOME}/.vcpkg-clion/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")

cmake_minimum_required(VERSION 4.1)
project(trading)

set(CMAKE_CXX_STANDARD 26)

enable_testing()

add_subdirectory(core)
add_subdirectory(orderbook)
add_subdirectory(networking)
