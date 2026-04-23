add_library(networking_itch INTERFACE messages.h)
target_include_directories(networking_itch INTERFACE ${CMAKE_SOURCE_DIR})
