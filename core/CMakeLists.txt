add_subdirectory(util)
add_subdirectory(memory)
add_subdirectory(queue)
add_subdirectory(logging)

# Umbrella target: link "core" to get all core sub-components
add_library(core INTERFACE)
target_link_libraries(core INTERFACE core_util core_memory core_queue core_logging)
