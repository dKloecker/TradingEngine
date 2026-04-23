add_subdirectory(itch)

add_library(networking INTERFACE)
target_link_libraries(networking INTERFACE networking_itch)
